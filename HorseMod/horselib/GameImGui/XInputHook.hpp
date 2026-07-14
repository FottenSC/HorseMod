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
#include <intrin.h>

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <array>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Horse::GameImGui
{
    class XInputHook
    {
    public:
        enum class HookedApi : uint8_t
        {
            None = 0,
            XInput14 = 1,
            XInput13 = 2,
            XInput910 = 3,
        };

        enum class ObservablePulsePhase : uint8_t
        {
            Idle = 0,
            Neutral,
            Press,
            Release,
            AwaitingCapabilities,
            Complete,
            Failed,
            Reset,
        };

        enum class ObservablePulseFailure : uint8_t
        {
            None = 0,
            InstallFailed,
            NoHookedStateApi,
            NoHookedCapabilitiesApi,
            InvalidPad,
            InvalidButtons,
            InvalidConnectivityProbe,
            EmptyPulse,
            DeviceNotConnected,
            InvalidStateOutput,
            CallerAborted,
        };

        struct ObservablePulseArm
        {
            DWORD selected_pad {0};
            WORD buttons {0};
            uint32_t neutral_reads {2};
            uint32_t press_reads {2};
            uint32_t release_reads {2};
            bool fake_connectivity {true};
            bool require_capabilities_read {false};
        };

        // Snapshot consumed by a closed-loop UI driver.  A phase acknowledgement
        // is tagged with the generation whose state was actually returned from a
        // hooked XInputGetState call; arming a pulse alone never acknowledges it.
        struct ObservablePulseStatus
        {
            uint64_t generation {0};
            ObservablePulsePhase phase {ObservablePulsePhase::Idle};
            ObservablePulseFailure failure {ObservablePulseFailure::None};
            DWORD selected_pad {0};
            WORD buttons {0};
            HookedApi selected_api {HookedApi::None};
            uint32_t hooked_state_api_mask {0};
            uint32_t hooked_capabilities_api_mask {0};
            uint32_t required_neutral_reads {0};
            uint32_t required_press_reads {0};
            uint32_t required_release_reads {0};
            uint64_t generation_state_reads {0};
            uint64_t generation_other_api_reads {0};
            uint64_t generation_capabilities_reads {0};
            uint64_t neutral_reads {0};
            uint64_t press_reads {0};
            uint64_t release_reads {0};
            uint64_t awaiting_capabilities_state_reads {0};
            uint64_t original_connected_reads {0};
            uint64_t original_disconnected_reads {0};
            uint64_t forced_connected_state_reads {0};
            uint64_t original_capabilities_success_reads {0};
            uint64_t original_capabilities_failure_reads {0};
            uint64_t forced_capabilities_reads {0};
            uint64_t invalid_output_reads {0};
            uint64_t native_force_rescan_writes {0};
            uint64_t connectivity_lease_generation {0};
            uint64_t connectivity_lease_state_reads {0};
            uint64_t connectivity_lease_forced_state_reads {0};
            uint64_t connectivity_lease_capabilities_reads {0};
            uint64_t connectivity_lease_forced_capabilities_reads {0};
            uint64_t neutral_ack_generation {0};
            uint64_t press_ack_generation {0};
            uint64_t release_ack_generation {0};
            uint64_t fake_state_ack_generation {0};
            uint64_t fake_capabilities_ack_generation {0};
            uint64_t completion_generation {0};
            uint64_t failure_generation {0};
            uint64_t reset_generation {0};
            std::array<uint64_t, 3> state_api_reads {};
            std::array<uint64_t, 3> capabilities_api_reads {};
            DWORD connectivity_lease_pad {0};
            HookedApi connectivity_lease_api {HookedApi::None};
            bool installed {false};
            bool native_poller_entry_verified {false};
            bool native_poller_hook_installed {false};
            bool connectivity_lease_active {false};
            bool active {false};
            bool fake_connectivity {false};
            bool require_capabilities_read {false};
        };

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

            m_hooked_state_api_mask.store(0, std::memory_order_release);
            m_hooked_capabilities_api_mask.store(0,
                                                  std::memory_order_release);

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
            void* capabilities_target = reinterpret_cast<void*>(
                GetProcAddress(hXInput, "XInputGetCapabilities"));

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
            m_hooked_state_api_mask.fetch_or(
                api_bit(HookedApi::XInput14), std::memory_order_acq_rel);
            if (capabilities_target)
            {
                m_capabilities_trampoline = 0;
                m_capabilities_detour = std::make_unique<PLH::x64Detour>(
                    reinterpret_cast<uint64_t>(capabilities_target),
                    reinterpret_cast<uint64_t>(
                        &XInputHook::detour_xinputgetcapabilities),
                    &m_capabilities_trampoline);
                if (!m_capabilities_detour->hook())
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] x64Detour::hook() failed "
                            "on XInputGetCapabilities (target=0x{:X}); "
                            "scripted input can still use GetState if SC6 "
                            "polls it.\n"),
                        reinterpret_cast<uintptr_t>(capabilities_target));
                    m_capabilities_detour.reset();
                    m_capabilities_trampoline = 0;
                }
                else
                {
                    m_hooked_capabilities_api_mask.fetch_or(
                        api_bit(HookedApi::XInput14),
                        std::memory_order_acq_rel);
                }
            }

            (void)install_additional_module(
                L"XINPUT1_3.dll",
                m_detour_13,
                m_trampoline_13,
                m_capabilities_detour_13,
                m_capabilities_trampoline_13,
                reinterpret_cast<uint64_t>(
                    &XInputHook::detour_xinputgetstate_13),
                reinterpret_cast<uint64_t>(
                    &XInputHook::detour_xinputgetcapabilities_13));
            if (m_trampoline_13)
                m_hooked_state_api_mask.fetch_or(
                    api_bit(HookedApi::XInput13),
                    std::memory_order_acq_rel);
            if (m_capabilities_trampoline_13)
                m_hooked_capabilities_api_mask.fetch_or(
                    api_bit(HookedApi::XInput13),
                    std::memory_order_acq_rel);
            (void)install_additional_module(
                L"XINPUT9_1_0.dll",
                m_detour_910,
                m_trampoline_910,
                m_capabilities_detour_910,
                m_capabilities_trampoline_910,
                reinterpret_cast<uint64_t>(
                    &XInputHook::detour_xinputgetstate_910),
                reinterpret_cast<uint64_t>(
                    &XInputHook::detour_xinputgetcapabilities_910));
            if (m_trampoline_910)
                m_hooked_state_api_mask.fetch_or(
                    api_bit(HookedApi::XInput910),
                    std::memory_order_acq_rel);
            if (m_capabilities_trampoline_910)
                m_hooked_capabilities_api_mask.fetch_or(
                    api_bit(HookedApi::XInput910),
                    std::memory_order_acq_rel);

            // SC6 only calls XInputGetState for a previously disconnected pad
            // when FXInputInterfaceCtx_Partial::fForcePoll is set.  The native
            // poller hook is fingerprinted to the documented retail function
            // at SoulcaliburVI.exe+0xE1EC00 and is otherwise inert.  While an
            // observable pulse is active it requests exactly one native rescan
            // per poll, allowing a test-owned virtual pad to become visible.
            (void)install_native_poller_hook();

            m_installed.store(true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] installed (target=0x{:X}, "
                    "trampoline=0x{:X}, capabilities=0x{:X})\n"),
                reinterpret_cast<uintptr_t>(target),
                static_cast<uintptr_t>(m_trampoline),
                reinterpret_cast<uintptr_t>(capabilities_target));
            return true;
        }

        // Uninstall on mod teardown.  Safe if install never succeeded.
        // SEH-wrapped unHook for the same reason as PresentHook —
        // see the long comment above try_unhook_seh in PresentHook.hpp.
        void uninstall()
        {
            (void)end_observable_connectivity_lease();
            (void)reset_observable_pulse();
            if (!m_installed.exchange(false))
            {
                m_hooked_state_api_mask.store(0, std::memory_order_release);
                m_hooked_capabilities_api_mask.store(
                    0, std::memory_order_release);
                return;
            }
            if (m_native_poller_detour)
            {
                if (!try_unhook_seh(m_native_poller_detour.get()))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] native poller unHook "
                            "faulted during teardown - swallowed.\n"));
                }
                m_native_poller_detour.reset();
                m_native_poller_trampoline = 0;
                m_native_poller_hook_installed.store(
                    false, std::memory_order_release);
            }
            if (m_detour)
            {
                if (!try_unhook_seh(m_detour.get()))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] x64Detour::unHook "
                            "faulted during teardown — swallowed.\n"));
                }
                m_detour.reset();
            }
            if (m_capabilities_detour)
            {
                if (!try_unhook_seh(m_capabilities_detour.get()))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] capabilities unHook "
                            "faulted during teardown - swallowed.\n"));
                }
                m_capabilities_detour.reset();
            }
            uninstall_additional_module(m_detour_13, m_capabilities_detour_13);
            uninstall_additional_module(
                m_detour_910,
                m_capabilities_detour_910);
            m_trampoline = 0;
            m_capabilities_trampoline = 0;
            m_trampoline_13 = 0;
            m_capabilities_trampoline_13 = 0;
            m_trampoline_910 = 0;
            m_capabilities_trampoline_910 = 0;
            m_hooked_state_api_mask.store(0, std::memory_order_release);
            m_hooked_capabilities_api_mask.store(0,
                                                  std::memory_order_release);
        }

    private:
        // Free function so __try/__except has no C++ destructors in scope.
        static bool try_unhook_seh(PLH::x64Detour* h) noexcept
        {
            __try { h->unHook(); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool install_additional_module(
            const wchar_t* module_name,
            std::unique_ptr<PLH::x64Detour>& state_detour,
            uint64_t& state_trampoline,
            std::unique_ptr<PLH::x64Detour>& capabilities_detour,
            uint64_t& capabilities_trampoline,
            uint64_t state_detour_target,
            uint64_t capabilities_detour_target)
        {
            HMODULE hXInput = GetModuleHandleW(module_name);
            if (!hXInput)
            {
                hXInput = LoadLibraryW(module_name);
            }
            if (!hXInput)
            {
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[GameImGui.XInputHook] {} not available.\n"),
                    module_name);
                return false;
            }

            void* target = reinterpret_cast<void*>(
                GetProcAddress(hXInput, "XInputGetState"));
            if (!target)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui.XInputHook] {} has no "
                        "XInputGetState export.\n"),
                    module_name);
                return false;
            }

            state_trampoline = 0;
            state_detour = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(target),
                state_detour_target,
                &state_trampoline);
            if (!state_detour->hook())
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui.XInputHook] x64Detour::hook() failed "
                        "on {}!XInputGetState target=0x{:X}.\n"),
                    module_name,
                    reinterpret_cast<uintptr_t>(target));
                state_detour.reset();
                state_trampoline = 0;
                return false;
            }

            void* capabilities_target = reinterpret_cast<void*>(
                GetProcAddress(hXInput, "XInputGetCapabilities"));
            if (capabilities_target)
            {
                capabilities_trampoline = 0;
                capabilities_detour = std::make_unique<PLH::x64Detour>(
                    reinterpret_cast<uint64_t>(capabilities_target),
                    capabilities_detour_target,
                    &capabilities_trampoline);
                if (!capabilities_detour->hook())
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] x64Detour::hook() failed "
                            "on {}!XInputGetCapabilities target=0x{:X}; "
                            "scripted input can still use GetState if SC6 "
                            "polls it.\n"),
                        module_name,
                        reinterpret_cast<uintptr_t>(capabilities_target));
                    capabilities_detour.reset();
                    capabilities_trampoline = 0;
                }
            }

            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] installed {} GetState=0x{:X} "
                    "trampoline=0x{:X} capabilities=0x{:X}\n"),
                module_name,
                reinterpret_cast<uintptr_t>(target),
                static_cast<uintptr_t>(state_trampoline),
                reinterpret_cast<uintptr_t>(capabilities_target));
            return true;
        }

        void uninstall_additional_module(
            std::unique_ptr<PLH::x64Detour>& state_detour,
            std::unique_ptr<PLH::x64Detour>& capabilities_detour)
        {
            if (state_detour)
            {
                if (!try_unhook_seh(state_detour.get()))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] x64Detour::unHook "
                            "faulted during teardown - swallowed.\n"));
                }
                state_detour.reset();
            }
            if (capabilities_detour)
            {
                if (!try_unhook_seh(capabilities_detour.get()))
                {
                    RC::Output::send<RC::LogLevel::Warning>(
                        STR("[GameImGui.XInputHook] capabilities unHook "
                            "faulted during teardown - swallowed.\n"));
                }
                capabilities_detour.reset();
            }
        }
    public:

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

        uint64_t arm_observable_pulse(const ObservablePulseArm& arm)
        {
            std::lock_guard<std::mutex> control_lock(
                m_connectivity_lease_control_mutex);
            const bool hook_ready = install();
            cancel_legacy_script();

            std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
            const uint64_t generation = next_observable_generation_locked();
            m_observable_pulse = {};
            m_observable_pulse.generation = generation;
            m_observable_pulse.selected_pad = arm.selected_pad;
            m_observable_pulse.buttons = arm.buttons;
            m_observable_pulse.required_neutral_reads = arm.neutral_reads;
            m_observable_pulse.required_press_reads = arm.press_reads;
            m_observable_pulse.required_release_reads = arm.release_reads;
            m_observable_pulse.fake_connectivity = arm.fake_connectivity;
            m_observable_pulse.require_capabilities_read =
                arm.require_capabilities_read;
            if (m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                && arm.selected_pad == m_connectivity_lease_pad.load(
                                           std::memory_order_acquire))
            {
                m_observable_pulse.selected_api = static_cast<HookedApi>(
                    m_connectivity_lease_api.load(
                        std::memory_order_acquire));
            }
            copy_last_observable_generations_locked(m_observable_pulse);

            ObservablePulseFailure failure = ObservablePulseFailure::None;
            if (!hook_ready)
                failure = ObservablePulseFailure::InstallFailed;
            else if (m_hooked_state_api_mask.load(std::memory_order_acquire) == 0)
                failure = ObservablePulseFailure::NoHookedStateApi;
            else if (arm.require_capabilities_read
                     && m_hooked_capabilities_api_mask.load(
                            std::memory_order_acquire) == 0)
                failure = ObservablePulseFailure::NoHookedCapabilitiesApi;
            else if (arm.selected_pad >= XUSER_MAX_COUNT)
                failure = ObservablePulseFailure::InvalidPad;
            else if (arm.press_reads != 0 && arm.buttons == 0)
                failure = ObservablePulseFailure::InvalidButtons;
            else if (arm.require_capabilities_read
                     && !arm.fake_connectivity)
                failure = ObservablePulseFailure::InvalidConnectivityProbe;
            else if (arm.neutral_reads == 0 && arm.press_reads == 0
                     && arm.release_reads == 0
                     && !arm.require_capabilities_read)
                failure = ObservablePulseFailure::EmptyPulse;

            if (failure != ObservablePulseFailure::None)
            {
                fail_observable_pulse_locked(failure);
                return generation;
            }

            select_first_observable_phase_locked();
            publish_observable_phase_locked();
            return generation;
        }

        // Keep a single logical controller connected across a multi-step UI
        // transaction.  Individual observable pulses temporarily override the
        // neutral lease state, so SC6 sees one connection lifecycle instead of
        // a disconnect/reconnect for every menu key.
        uint64_t begin_observable_connectivity_lease(
            DWORD selected_pad = 0)
        {
            std::lock_guard<std::mutex> control_lock(
                m_connectivity_lease_control_mutex);
            if (!install() || selected_pad >= XUSER_MAX_COUNT)
                return 0;
            cancel_legacy_script();
            if (m_connectivity_lease_active.load(
                    std::memory_order_acquire))
            {
                // Beginning the same lease twice must not briefly publish a
                // disconnected controller between two XInput polls.  Changing
                // pads requires an explicit end followed by a new begin.
                return m_connectivity_lease_pad.load(
                           std::memory_order_acquire) == selected_pad
                    ? m_connectivity_lease_generation.load(
                          std::memory_order_acquire)
                    : 0;
            }
            if (observable_phase_active(static_cast<ObservablePulsePhase>(
                    m_observable_phase.load(std::memory_order_acquire))))
            {
                // A lease establishes the API lane for subsequent pulses.
                // Starting one around an in-flight pulse could strand that
                // pulse on a different lane.
                return 0;
            }
            uint64_t generation =
                m_connectivity_lease_generation_counter.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            if (generation == 0)
            {
                generation =
                    m_connectivity_lease_generation_counter.fetch_add(
                        1, std::memory_order_acq_rel) + 1;
            }
            m_connectivity_lease_active.store(
                false, std::memory_order_release);
            m_connectivity_lease_pad.store(
                selected_pad, std::memory_order_release);
            m_connectivity_lease_api.store(
                static_cast<uint8_t>(HookedApi::None),
                std::memory_order_release);
            m_connectivity_lease_state_reads.store(
                0, std::memory_order_release);
            m_connectivity_lease_forced_state_reads.store(
                0, std::memory_order_release);
            m_connectivity_lease_capabilities_reads.store(
                0, std::memory_order_release);
            m_connectivity_lease_forced_capabilities_reads.store(
                0, std::memory_order_release);
            m_connectivity_lease_generation.store(
                generation, std::memory_order_release);
            m_connectivity_lease_active.store(
                true, std::memory_order_release);
            return generation;
        }

        bool end_observable_connectivity_lease(
            uint64_t expected_generation = 0) noexcept
        {
            std::lock_guard<std::mutex> control_lock(
                m_connectivity_lease_control_mutex);
            const uint64_t generation =
                m_connectivity_lease_generation.load(
                    std::memory_order_acquire);
            if (!m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                || (expected_generation != 0
                    && generation != expected_generation))
            {
                return false;
            }
            m_connectivity_lease_active.store(
                false, std::memory_order_release);
            m_connectivity_lease_api.store(
                static_cast<uint8_t>(HookedApi::None),
                std::memory_order_release);
            return true;
        }

        uint64_t arm_observable_button_pulse(
            WORD buttons,
            DWORD selected_pad = 0,
            uint32_t neutral_reads = 2,
            uint32_t press_reads = 2,
            uint32_t release_reads = 2,
            bool fake_connectivity = true,
            bool require_capabilities_read = false)
        {
            ObservablePulseArm arm{};
            arm.selected_pad = selected_pad;
            arm.buttons = buttons;
            arm.neutral_reads = neutral_reads;
            arm.press_reads = press_reads;
            arm.release_reads = release_reads;
            arm.fake_connectivity = fake_connectivity;
            arm.require_capabilities_read = require_capabilities_read;
            return arm_observable_pulse(arm);
        }

        uint64_t arm_observable_neutral(
            DWORD selected_pad = 0,
            uint32_t required_reads = 2,
            bool fake_connectivity = true,
            bool require_capabilities_read = false)
        {
            ObservablePulseArm arm{};
            arm.selected_pad = selected_pad;
            arm.neutral_reads = required_reads;
            arm.press_reads = 0;
            arm.release_reads = 0;
            arm.fake_connectivity = fake_connectivity;
            arm.require_capabilities_read = require_capabilities_read;
            return arm_observable_pulse(arm);
        }

        uint64_t arm_observable_press(WORD buttons,
                                      DWORD selected_pad = 0,
                                      uint32_t required_reads = 2,
                                      bool fake_connectivity = true)
        {
            ObservablePulseArm arm{};
            arm.selected_pad = selected_pad;
            arm.buttons = buttons;
            arm.neutral_reads = 0;
            arm.press_reads = required_reads;
            arm.release_reads = 0;
            arm.fake_connectivity = fake_connectivity;
            return arm_observable_pulse(arm);
        }

        uint64_t arm_observable_release(DWORD selected_pad = 0,
                                        uint32_t required_reads = 2,
                                        bool fake_connectivity = true)
        {
            ObservablePulseArm arm{};
            arm.selected_pad = selected_pad;
            arm.neutral_reads = 0;
            arm.press_reads = 0;
            arm.release_reads = required_reads;
            arm.fake_connectivity = fake_connectivity;
            return arm_observable_pulse(arm);
        }

        uint64_t arm_fake_connectivity_probe(
            DWORD selected_pad = 0,
            uint32_t required_state_reads = 2,
            bool require_capabilities_read = true)
        {
            return arm_observable_neutral(selected_pad,
                                          required_state_reads,
                                          true,
                                          require_capabilities_read);
        }

        ObservablePulseStatus observable_pulse_status()
        {
            ObservablePulseStatus out{};
            {
                std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
                out = m_observable_pulse;
                copy_last_observable_generations_locked(out);
            }
            out.installed = m_installed.load(std::memory_order_acquire);
            out.native_poller_entry_verified =
                m_native_poller_entry_verified.load(
                    std::memory_order_acquire);
            out.native_poller_hook_installed =
                m_native_poller_hook_installed.load(
                    std::memory_order_acquire);
            out.native_force_rescan_writes =
                m_native_force_rescan_writes.load(
                    std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> control_lock(
                    m_connectivity_lease_control_mutex);
                out.connectivity_lease_generation =
                    m_connectivity_lease_generation.load(
                        std::memory_order_acquire);
                out.connectivity_lease_state_reads =
                    m_connectivity_lease_state_reads.load(
                        std::memory_order_acquire);
                out.connectivity_lease_forced_state_reads =
                    m_connectivity_lease_forced_state_reads.load(
                        std::memory_order_acquire);
                out.connectivity_lease_capabilities_reads =
                    m_connectivity_lease_capabilities_reads.load(
                        std::memory_order_acquire);
                out.connectivity_lease_forced_capabilities_reads =
                    m_connectivity_lease_forced_capabilities_reads.load(
                        std::memory_order_acquire);
                out.connectivity_lease_pad =
                    m_connectivity_lease_pad.load(
                        std::memory_order_acquire);
                out.connectivity_lease_api =
                    static_cast<HookedApi>(
                        m_connectivity_lease_api.load(
                            std::memory_order_acquire));
                out.connectivity_lease_active =
                    m_connectivity_lease_active.load(
                        std::memory_order_acquire);
            }
            out.hooked_state_api_mask =
                m_hooked_state_api_mask.load(std::memory_order_acquire);
            out.hooked_capabilities_api_mask =
                m_hooked_capabilities_api_mask.load(std::memory_order_acquire);
            for (size_t i = 0; i < out.state_api_reads.size(); ++i)
            {
                out.state_api_reads[i] =
                    m_state_api_reads[i].load(std::memory_order_acquire);
                out.capabilities_api_reads[i] =
                    m_capabilities_api_reads[i].load(
                        std::memory_order_acquire);
            }
            out.active = observable_phase_active(out.phase);
            return out;
        }

        bool fail_observable_pulse(
            uint64_t expected_generation,
            ObservablePulseFailure failure =
                ObservablePulseFailure::CallerAborted)
        {
            std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
            if (expected_generation == 0
                || m_observable_pulse.generation != expected_generation
                || !observable_phase_active(m_observable_pulse.phase))
            {
                return false;
            }
            if (failure == ObservablePulseFailure::None)
                failure = ObservablePulseFailure::CallerAborted;
            fail_observable_pulse_locked(failure);
            return true;
        }

        bool reset_observable_pulse(uint64_t expected_generation = 0)
        {
            std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
            if (expected_generation != 0
                && m_observable_pulse.generation != expected_generation)
            {
                return false;
            }
            const uint64_t generation = next_observable_generation_locked();
            m_observable_pulse = {};
            m_observable_pulse.generation = generation;
            m_observable_pulse.phase = ObservablePulsePhase::Reset;
            m_last_reset_generation = generation;
            copy_last_observable_generations_locked(m_observable_pulse);
            publish_observable_phase_locked();
            return true;
        }

        void start_button_script(const std::vector<WORD>& buttons,
                                 uint32_t press_ms = 90,
                                 uint32_t gap_ms = 90,
                                 bool all_pads = false)
        {
            (void)install();
            (void)reset_observable_pulse();
            std::lock_guard<std::mutex> lock(m_script_mutex);
            m_script.clear();
            m_script.reserve(buttons.size() * 2);
            const uint32_t clamped_press = press_ms ? press_ms : 90;
            const uint32_t clamped_gap = gap_ms ? gap_ms : 90;
            for (const WORD button : buttons)
            {
                if (!button) continue;
                m_script.push_back({button, clamped_press});
                m_script.push_back({0, clamped_gap});
            }
            m_script_start_ms = GetTickCount64();
            ULONGLONG total_ms = 0;
            for (const ScriptStep& step : m_script)
                total_ms += step.duration_ms;
            const ULONGLONG fake_until = m_script_start_ms + total_ms + 30000;
            extend_fake_controller_window_until(fake_until);
            m_script_state_reads.store(0, std::memory_order_release);
            m_script_capability_reads.store(0, std::memory_order_release);
            m_fake_state_reads.store(0, std::memory_order_release);
            m_xinput_caller_log_budget.store(32, std::memory_order_release);
            m_fake_all_pads.store(all_pads, std::memory_order_release);
            m_script_active.store(!m_script.empty(), std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] queued scripted input pulses={} "
                    "press_ms={} gap_ms={} fake_ms={} all_pads={}\n"),
                buttons.size(),
                clamped_press,
                clamped_gap,
                static_cast<unsigned long long>(
                    fake_until - m_script_start_ms),
                all_pads ? 1 : 0);
        }

        void start_fake_controller_window(uint32_t fake_ms = 30000,
                                          bool all_pads = false)
        {
            (void)install();
            (void)reset_observable_pulse();
            const uint32_t clamped_ms = fake_ms ? fake_ms : 30000;
            const ULONGLONG now = GetTickCount64();
            extend_fake_controller_window_until(now + clamped_ms);
            m_script_capability_reads.store(0, std::memory_order_release);
            m_fake_state_reads.store(0, std::memory_order_release);
            m_xinput_caller_log_budget.store(32, std::memory_order_release);
            m_fake_all_pads.store(all_pads, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] fake controller window ms={} "
                    "all_pads={}\n"),
                clamped_ms,
                all_pads ? 1 : 0);
        }

    private:
        XInputHook() = default;
        ~XInputHook() { uninstall(); }
        XInputHook(const XInputHook&)            = delete;
        XInputHook& operator=(const XInputHook&) = delete;

        using NativePollAllPadsFn = void(__fastcall*)(void*);

        bool install_native_poller_hook()
        {
            if (m_native_poller_hook_installed.load(
                    std::memory_order_acquire))
            {
                return true;
            }

            static constexpr uintptr_t kNativePollerRva = 0x00E1EC00u;
            static constexpr std::array<uint8_t, 16> kExpectedEntry {{
                0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48,
                0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
            }};

            HMODULE executable = GetModuleHandleW(nullptr);
            if (!executable)
                return false;
            auto* target = reinterpret_cast<uint8_t*>(executable)
                + kNativePollerRva;
            MEMORY_BASIC_INFORMATION memory_info {};
            if (VirtualQuery(target, &memory_info, sizeof(memory_info)) == 0
                || memory_info.State != MEM_COMMIT
                || (memory_info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
                || static_cast<size_t>(
                       static_cast<uint8_t*>(memory_info.BaseAddress)
                       + memory_info.RegionSize - target)
                       < kExpectedEntry.size()
                || std::memcmp(target,
                               kExpectedEntry.data(),
                               kExpectedEntry.size()) != 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui.XInputHook] SC6 native pad poller "
                        "fingerprint mismatch; disconnected-pad force "
                        "rescan disabled.\n"));
                return false;
            }
            m_native_poller_entry_verified.store(
                true, std::memory_order_release);

            m_native_poller_trampoline = 0;
            m_native_poller_detour = std::make_unique<PLH::x64Detour>(
                reinterpret_cast<uint64_t>(target),
                reinterpret_cast<uint64_t>(
                    &XInputHook::detour_native_poll_all_pads),
                &m_native_poller_trampoline);
            if (!m_native_poller_detour->hook()
                || m_native_poller_trampoline == 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui.XInputHook] SC6 native pad poller hook "
                        "failed; disconnected-pad force rescan disabled.\n"));
                m_native_poller_detour.reset();
                m_native_poller_trampoline = 0;
                return false;
            }
            m_native_poller_hook_installed.store(
                true, std::memory_order_release);
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] installed verified SC6 native "
                    "pad poller rescan hook (target=0x{:X}, "
                    "trampoline=0x{:X})\n"),
                reinterpret_cast<uintptr_t>(target),
                static_cast<uintptr_t>(m_native_poller_trampoline));
            return true;
        }

        static void __fastcall detour_native_poll_all_pads(void* context)
        {
            auto& self = instance();
            const ObservablePulsePhase phase =
                static_cast<ObservablePulsePhase>(
                    self.m_observable_phase.load(
                        std::memory_order_acquire));
            const bool lease_needs_initial_scan =
                self.m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                && self.m_connectivity_lease_state_reads.load(
                       std::memory_order_acquire) == 0;
            if (context
                && (observable_phase_active(phase)
                    || lease_needs_initial_scan))
            {
                // Ghidra: FXInputInterfaceCtx_Partial::fForcePoll is the
                // byte at +0.  The native function clears it after this poll.
                *static_cast<volatile uint8_t*>(context) = 1;
                self.m_native_force_rescan_writes.fetch_add(
                    1, std::memory_order_acq_rel);
            }

            auto original = reinterpret_cast<NativePollAllPadsFn>(
                self.m_native_poller_trampoline);
            if (original)
                original(context);
        }

        // ---- The actual detour --------------------------------------
        static DWORD WINAPI detour_xinputgetstate(DWORD pad_index,
                                                  XINPUT_STATE* out_state)
        {
            auto& self = instance();
            self.log_xinput_caller(L"xinput1_4!XInputGetState",
                                   _ReturnAddress());
            self.note_state_api_read(HookedApi::XInput14);
            auto orig = reinterpret_cast<
                DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(self.m_trampoline);
            const DWORD r = orig ? orig(pad_index, out_state)
                                  : XInputGetState(pad_index, out_state);

            if (self.apply_observable_state(HookedApi::XInput14,
                                            pad_index,
                                            r,
                                            out_state))
                return ERROR_SUCCESS;

            if (self.apply_observable_connectivity_lease(
                    HookedApi::XInput14, pad_index, r, out_state))
                return ERROR_SUCCESS;

            if (self.fake_controller_active(pad_index) && out_state
                && self.apply_scripted_state(out_state))
                return ERROR_SUCCESS;

            if (out_state && self.fake_controller_active(pad_index)
                && r != ERROR_SUCCESS)
            {
                self.write_zero_fake_state(out_state);
                return ERROR_SUCCESS;
            }

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

        static DWORD WINAPI detour_xinputgetcapabilities(
            DWORD pad_index,
            DWORD flags,
            XINPUT_CAPABILITIES* out_capabilities)
        {
            auto& self = instance();
            self.log_xinput_caller(L"xinput1_4!XInputGetCapabilities",
                                   _ReturnAddress());
            self.note_capabilities_api_read(HookedApi::XInput14);
            auto orig = reinterpret_cast<
                DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*)>(
                    self.m_capabilities_trampoline);
            const DWORD r = orig
                ? orig(pad_index, flags, out_capabilities)
                : ERROR_DEVICE_NOT_CONNECTED;
            if (self.apply_observable_capabilities(HookedApi::XInput14,
                                                   pad_index,
                                                   r,
                                                   out_capabilities))
                return ERROR_SUCCESS;
            if (self.apply_observable_connectivity_lease_capabilities(
                    HookedApi::XInput14,
                    pad_index,
                    r,
                    out_capabilities))
                return ERROR_SUCCESS;
            if (self.fake_controller_active(pad_index))
            {
                if (out_capabilities)
                {
                    *out_capabilities = {};
                    out_capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
                    out_capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                }
                const DWORD reads = self.m_script_capability_reads.fetch_add(
                    1, std::memory_order_acq_rel);
                if (reads == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[GameImGui.XInputHook] scripted fake controller "
                            "capability probe accepted pad={}\n"),
                        pad_index);
                }
                return ERROR_SUCCESS;
            }
            return r;
        }

        static DWORD WINAPI detour_xinputgetstate_13(
            DWORD pad_index,
            XINPUT_STATE* out_state)
        {
            auto& self = instance();
            self.log_xinput_caller(L"XINPUT1_3!XInputGetState",
                                   _ReturnAddress());
            return self.detour_xinputgetstate_from(
                HookedApi::XInput13,
                self.m_trampoline_13,
                pad_index,
                out_state);
        }

        static DWORD WINAPI detour_xinputgetstate_910(
            DWORD pad_index,
            XINPUT_STATE* out_state)
        {
            auto& self = instance();
            self.log_xinput_caller(L"XINPUT9_1_0!XInputGetState",
                                   _ReturnAddress());
            return self.detour_xinputgetstate_from(
                HookedApi::XInput910,
                self.m_trampoline_910,
                pad_index,
                out_state);
        }

        static DWORD WINAPI detour_xinputgetcapabilities_13(
            DWORD pad_index,
            DWORD flags,
            XINPUT_CAPABILITIES* out_capabilities)
        {
            auto& self = instance();
            self.log_xinput_caller(L"XINPUT1_3!XInputGetCapabilities",
                                   _ReturnAddress());
            return self.detour_xinputgetcapabilities_from(
                HookedApi::XInput13,
                self.m_capabilities_trampoline_13,
                pad_index,
                flags,
                out_capabilities);
        }

        static DWORD WINAPI detour_xinputgetcapabilities_910(
            DWORD pad_index,
            DWORD flags,
            XINPUT_CAPABILITIES* out_capabilities)
        {
            auto& self = instance();
            self.log_xinput_caller(L"XINPUT9_1_0!XInputGetCapabilities",
                                   _ReturnAddress());
            return self.detour_xinputgetcapabilities_from(
                HookedApi::XInput910,
                self.m_capabilities_trampoline_910,
                pad_index,
                flags,
                out_capabilities);
        }

        DWORD detour_xinputgetstate_from(
            HookedApi api,
            uint64_t trampoline,
            DWORD pad_index,
            XINPUT_STATE* out_state)
        {
            note_state_api_read(api);
            auto orig = reinterpret_cast<
                DWORD(WINAPI*)(DWORD, XINPUT_STATE*)>(trampoline);
            const DWORD r = orig ? orig(pad_index, out_state)
                                  : ERROR_DEVICE_NOT_CONNECTED;

            if (apply_observable_state(api, pad_index, r, out_state))
                return ERROR_SUCCESS;

            if (apply_observable_connectivity_lease(
                    api, pad_index, r, out_state))
                return ERROR_SUCCESS;

            if (fake_controller_active(pad_index) && out_state
                && apply_scripted_state(out_state))
                return ERROR_SUCCESS;

            if (out_state && fake_controller_active(pad_index)
                && r != ERROR_SUCCESS)
            {
                write_zero_fake_state(out_state);
                return ERROR_SUCCESS;
            }

            if (r == ERROR_SUCCESS && out_state &&
                g_overlay_visible.load(std::memory_order_relaxed))
            {
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

        DWORD detour_xinputgetcapabilities_from(
            HookedApi api,
            uint64_t trampoline,
            DWORD pad_index,
            DWORD flags,
            XINPUT_CAPABILITIES* out_capabilities)
        {
            note_capabilities_api_read(api);
            auto orig = reinterpret_cast<
                DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*)>(
                    trampoline);
            const DWORD r = orig
                ? orig(pad_index, flags, out_capabilities)
                : ERROR_DEVICE_NOT_CONNECTED;
            if (apply_observable_capabilities(api,
                                              pad_index,
                                              r,
                                              out_capabilities))
                return ERROR_SUCCESS;
            if (apply_observable_connectivity_lease_capabilities(
                    api, pad_index, r, out_capabilities))
                return ERROR_SUCCESS;
            if (fake_controller_active(pad_index))
            {
                if (out_capabilities)
                {
                    *out_capabilities = {};
                    out_capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
                    out_capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
                }
                const DWORD reads = m_script_capability_reads.fetch_add(
                    1, std::memory_order_acq_rel);
                if (reads == 0)
                {
                    RC::Output::send<RC::LogLevel::Default>(
                        STR("[GameImGui.XInputHook] scripted fake controller "
                            "capability probe accepted pad={}\n"),
                        pad_index);
                }
                return ERROR_SUCCESS;
            }
            return r;
        }

        static constexpr uint32_t api_bit(HookedApi api) noexcept
        {
            const uint32_t value = static_cast<uint32_t>(api);
            return value >= 1 && value <= 3 ? (1u << (value - 1u)) : 0u;
        }

        static constexpr size_t api_index(HookedApi api) noexcept
        {
            const size_t value = static_cast<size_t>(api);
            return value >= 1 && value <= 3 ? value - 1u : 3u;
        }

        static bool observable_phase_active(
            ObservablePulsePhase phase) noexcept
        {
            return phase == ObservablePulsePhase::Neutral
                || phase == ObservablePulsePhase::Press
                || phase == ObservablePulsePhase::Release
                || phase == ObservablePulsePhase::AwaitingCapabilities;
        }

        void note_state_api_read(HookedApi api) noexcept
        {
            const size_t index = api_index(api);
            if (index < m_state_api_reads.size())
                m_state_api_reads[index].fetch_add(
                    1, std::memory_order_acq_rel);
        }

        void note_capabilities_api_read(HookedApi api) noexcept
        {
            const size_t index = api_index(api);
            if (index < m_capabilities_api_reads.size())
                m_capabilities_api_reads[index].fetch_add(
                    1, std::memory_order_acq_rel);
        }

        uint64_t next_observable_generation_locked() noexcept
        {
            ++m_observable_generation_counter;
            if (m_observable_generation_counter == 0)
                ++m_observable_generation_counter;
            return m_observable_generation_counter;
        }

        void copy_last_observable_generations_locked(
            ObservablePulseStatus& status) const noexcept
        {
            status.neutral_ack_generation = m_last_neutral_ack_generation;
            status.press_ack_generation = m_last_press_ack_generation;
            status.release_ack_generation = m_last_release_ack_generation;
            status.fake_state_ack_generation =
                m_last_fake_state_ack_generation;
            status.fake_capabilities_ack_generation =
                m_last_fake_capabilities_ack_generation;
            status.completion_generation = m_last_completion_generation;
            status.failure_generation = m_last_failure_generation;
            status.reset_generation = m_last_reset_generation;
        }

        void publish_observable_phase_locked() noexcept
        {
            m_observable_phase.store(
                static_cast<uint8_t>(m_observable_pulse.phase),
                std::memory_order_release);
        }

        void complete_observable_pulse_locked() noexcept
        {
            m_observable_pulse.phase = ObservablePulsePhase::Complete;
            m_observable_pulse.failure = ObservablePulseFailure::None;
            m_last_completion_generation = m_observable_pulse.generation;
            copy_last_observable_generations_locked(m_observable_pulse);
            publish_observable_phase_locked();
        }

        void fail_observable_pulse_locked(
            ObservablePulseFailure failure) noexcept
        {
            m_observable_pulse.phase = ObservablePulsePhase::Failed;
            m_observable_pulse.failure = failure;
            m_last_failure_generation = m_observable_pulse.generation;
            copy_last_observable_generations_locked(m_observable_pulse);
            publish_observable_phase_locked();
        }

        void finish_observable_phases_locked() noexcept
        {
            if (m_observable_pulse.require_capabilities_read
                && m_last_fake_capabilities_ack_generation
                    != m_observable_pulse.generation)
            {
                m_observable_pulse.phase =
                    ObservablePulsePhase::AwaitingCapabilities;
                publish_observable_phase_locked();
                return;
            }
            complete_observable_pulse_locked();
        }

        void select_first_observable_phase_locked() noexcept
        {
            if (m_observable_pulse.required_neutral_reads != 0)
                m_observable_pulse.phase = ObservablePulsePhase::Neutral;
            else if (m_observable_pulse.required_press_reads != 0)
                m_observable_pulse.phase = ObservablePulsePhase::Press;
            else if (m_observable_pulse.required_release_reads != 0)
                m_observable_pulse.phase = ObservablePulsePhase::Release;
            else
                finish_observable_phases_locked();
        }

        void advance_observable_phase_locked(
            ObservablePulsePhase completed_phase) noexcept
        {
            if (completed_phase == ObservablePulsePhase::Neutral)
            {
                m_last_neutral_ack_generation =
                    m_observable_pulse.generation;
                if (m_observable_pulse.required_press_reads != 0)
                    m_observable_pulse.phase = ObservablePulsePhase::Press;
                else if (m_observable_pulse.required_release_reads != 0)
                    m_observable_pulse.phase = ObservablePulsePhase::Release;
                else
                    finish_observable_phases_locked();
            }
            else if (completed_phase == ObservablePulsePhase::Press)
            {
                m_last_press_ack_generation = m_observable_pulse.generation;
                if (m_observable_pulse.required_release_reads != 0)
                    m_observable_pulse.phase = ObservablePulsePhase::Release;
                else
                    finish_observable_phases_locked();
            }
            else if (completed_phase == ObservablePulsePhase::Release)
            {
                m_last_release_ack_generation =
                    m_observable_pulse.generation;
                finish_observable_phases_locked();
            }
            copy_last_observable_generations_locked(m_observable_pulse);
            publish_observable_phase_locked();
        }

        bool claim_observable_connectivity_lease_api(
            HookedApi api,
            DWORD pad_index) noexcept
        {
            if (!m_connectivity_lease_active.load(
                    std::memory_order_acquire))
            {
                return false;
            }

            const uint64_t generation =
                m_connectivity_lease_generation.load(
                    std::memory_order_acquire);
            if (generation == 0
                || pad_index != m_connectivity_lease_pad.load(
                                    std::memory_order_acquire))
            {
                return false;
            }

            uint8_t selected_api = m_connectivity_lease_api.load(
                std::memory_order_acquire);
            if (selected_api
                == static_cast<uint8_t>(HookedApi::None))
            {
                uint8_t expected =
                    static_cast<uint8_t>(HookedApi::None);
                (void)m_connectivity_lease_api.compare_exchange_strong(
                    expected,
                    static_cast<uint8_t>(api),
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
                selected_api = m_connectivity_lease_api.load(
                    std::memory_order_acquire);
            }

            // Recheck the publication token after claiming the lane.  This
            // prevents an in-flight call from applying an ended or replaced
            // lease to a later XInput result.
            return selected_api == static_cast<uint8_t>(api)
                && m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                && generation == m_connectivity_lease_generation.load(
                    std::memory_order_acquire)
                && pad_index == m_connectivity_lease_pad.load(
                    std::memory_order_acquire);
        }

        bool apply_observable_state(HookedApi api,
                                    DWORD pad_index,
                                    DWORD original_result,
                                    XINPUT_STATE* out_state)
        {
            const ObservablePulsePhase published =
                static_cast<ObservablePulsePhase>(
                    m_observable_phase.load(std::memory_order_acquire));
            if (!observable_phase_active(published))
                return false;

            std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
            if (!observable_phase_active(m_observable_pulse.phase)
                || pad_index != m_observable_pulse.selected_pad)
            {
                return false;
            }
            const bool matching_lease =
                m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                && pad_index == m_connectivity_lease_pad.load(
                                    std::memory_order_acquire);
            if (matching_lease
                && !claim_observable_connectivity_lease_api(api, pad_index))
            {
                ++m_observable_pulse.generation_other_api_reads;
                return false;
            }
            if (m_observable_pulse.selected_api == HookedApi::None)
            {
                m_observable_pulse.selected_api = api;
            }
            else if (m_observable_pulse.selected_api != api)
            {
                ++m_observable_pulse.generation_other_api_reads;
                return false;
            }

            if (!out_state)
            {
                ++m_observable_pulse.invalid_output_reads;
                fail_observable_pulse_locked(
                    ObservablePulseFailure::InvalidStateOutput);
                return false;
            }

            if (original_result == ERROR_SUCCESS)
                ++m_observable_pulse.original_connected_reads;
            else
            {
                ++m_observable_pulse.original_disconnected_reads;
                if (!m_observable_pulse.fake_connectivity)
                {
                    fail_observable_pulse_locked(
                        ObservablePulseFailure::DeviceNotConnected);
                    return false;
                }
                ++m_observable_pulse.forced_connected_state_reads;
            }

            const ObservablePulsePhase served_phase =
                m_observable_pulse.phase;
            const WORD served_buttons =
                served_phase == ObservablePulsePhase::Press
                    ? m_observable_pulse.buttons
                    : 0;
            const DWORD packet = m_script_packet.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            *out_state = {};
            out_state->dwPacketNumber = packet;
            out_state->Gamepad.wButtons = served_buttons;
            ++m_observable_pulse.generation_state_reads;
            if (m_observable_pulse.fake_connectivity)
                m_last_fake_state_ack_generation =
                    m_observable_pulse.generation;

            if (served_phase == ObservablePulsePhase::Neutral)
            {
                ++m_observable_pulse.neutral_reads;
                if (m_observable_pulse.neutral_reads >=
                    m_observable_pulse.required_neutral_reads)
                {
                    advance_observable_phase_locked(served_phase);
                }
            }
            else if (served_phase == ObservablePulsePhase::Press)
            {
                ++m_observable_pulse.press_reads;
                if (m_observable_pulse.press_reads >=
                    m_observable_pulse.required_press_reads)
                {
                    advance_observable_phase_locked(served_phase);
                }
            }
            else if (served_phase == ObservablePulsePhase::Release)
            {
                ++m_observable_pulse.release_reads;
                if (m_observable_pulse.release_reads >=
                    m_observable_pulse.required_release_reads)
                {
                    advance_observable_phase_locked(served_phase);
                }
            }
            else
            {
                ++m_observable_pulse.awaiting_capabilities_state_reads;
            }

            copy_last_observable_generations_locked(m_observable_pulse);
            return true;
        }

        bool apply_observable_connectivity_lease(
            HookedApi api,
            DWORD pad_index,
            DWORD original_result,
            XINPUT_STATE* out_state) noexcept
        {
            if (!out_state
                || !claim_observable_connectivity_lease_api(
                    api, pad_index))
            {
                return false;
            }

            const DWORD packet = m_script_packet.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            *out_state = {};
            out_state->dwPacketNumber = packet;
            m_connectivity_lease_state_reads.fetch_add(
                1, std::memory_order_acq_rel);
            if (original_result != ERROR_SUCCESS)
            {
                m_connectivity_lease_forced_state_reads.fetch_add(
                    1, std::memory_order_acq_rel);
            }
            return true;
        }

        bool apply_observable_connectivity_lease_capabilities(
            HookedApi api,
            DWORD pad_index,
            DWORD original_result,
            XINPUT_CAPABILITIES* out_capabilities) noexcept
        {
            if (!out_capabilities
                || !claim_observable_connectivity_lease_api(
                    api, pad_index))
            {
                return false;
            }

            *out_capabilities = {};
            out_capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
            out_capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
            m_connectivity_lease_capabilities_reads.fetch_add(
                1, std::memory_order_acq_rel);
            if (original_result != ERROR_SUCCESS)
            {
                m_connectivity_lease_forced_capabilities_reads.fetch_add(
                    1, std::memory_order_acq_rel);
            }
            return true;
        }

        bool apply_observable_capabilities(
            HookedApi api,
            DWORD pad_index,
            DWORD original_result,
            XINPUT_CAPABILITIES* out_capabilities)
        {
            const ObservablePulsePhase published =
                static_cast<ObservablePulsePhase>(
                    m_observable_phase.load(std::memory_order_acquire));
            if (!observable_phase_active(published))
                return false;

            std::lock_guard<std::mutex> lock(m_observable_pulse_mutex);
            if (!observable_phase_active(m_observable_pulse.phase)
                || pad_index != m_observable_pulse.selected_pad
                || !m_observable_pulse.fake_connectivity)
            {
                return false;
            }
            const bool matching_lease =
                m_connectivity_lease_active.load(
                    std::memory_order_acquire)
                && pad_index == m_connectivity_lease_pad.load(
                                    std::memory_order_acquire);
            if (matching_lease
                && !claim_observable_connectivity_lease_api(api, pad_index))
            {
                ++m_observable_pulse.generation_other_api_reads;
                return false;
            }
            if (m_observable_pulse.selected_api == HookedApi::None)
                m_observable_pulse.selected_api = api;
            else if (m_observable_pulse.selected_api != api)
            {
                ++m_observable_pulse.generation_other_api_reads;
                return false;
            }
            if (!out_capabilities)
            {
                ++m_observable_pulse.invalid_output_reads;
                return false;
            }

            *out_capabilities = {};
            out_capabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
            out_capabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
            ++m_observable_pulse.generation_capabilities_reads;
            if (original_result == ERROR_SUCCESS)
                ++m_observable_pulse.original_capabilities_success_reads;
            else
                ++m_observable_pulse.original_capabilities_failure_reads;
            ++m_observable_pulse.forced_capabilities_reads;
            m_last_fake_capabilities_ack_generation =
                m_observable_pulse.generation;
            copy_last_observable_generations_locked(m_observable_pulse);
            if (m_observable_pulse.phase ==
                    ObservablePulsePhase::AwaitingCapabilities
                && m_observable_pulse.require_capabilities_read)
            {
                complete_observable_pulse_locked();
            }
            return true;
        }

        void cancel_legacy_script()
        {
            std::lock_guard<std::mutex> lock(m_script_mutex);
            m_script.clear();
            m_script_active.store(false, std::memory_order_release);
            m_fake_until_ms.store(0, std::memory_order_release);
            m_fake_all_pads.store(false, std::memory_order_release);
        }

        struct ScriptStep
        {
            WORD     buttons {0};
            uint32_t duration_ms {0};
        };

        bool apply_scripted_state(XINPUT_STATE* out_state)
        {
            if (!m_script_active.load(std::memory_order_acquire))
                return false;

            std::lock_guard<std::mutex> lock(m_script_mutex);
            if (m_script.empty())
            {
                m_script_active.store(false, std::memory_order_release);
                return false;
            }

            const ULONGLONG elapsed = GetTickCount64() - m_script_start_ms;
            ULONGLONG cursor = 0;
            WORD buttons = 0;
            bool found = false;
            for (const ScriptStep& step : m_script)
            {
                cursor += step.duration_ms;
                if (elapsed < cursor)
                {
                    buttons = step.buttons;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                m_script.clear();
                m_script_active.store(false, std::memory_order_release);
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[GameImGui.XInputHook] scripted input complete\n"));
                return false;
            }

            const DWORD packet = m_script_packet.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            *out_state = {};
            out_state->dwPacketNumber = packet;
            out_state->Gamepad.wButtons = buttons;
            const DWORD reads = m_script_state_reads.fetch_add(
                1, std::memory_order_acq_rel);
            if (reads == 0)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[GameImGui.XInputHook] scripted input reached "
                        "XInputGetState\n"));
            }
            return true;
        }

        bool fake_controller_active(DWORD pad_index) const noexcept
        {
            const bool allowed_pad =
                m_fake_all_pads.load(std::memory_order_acquire)
                    ? pad_index < 4
                    : pad_index == 0;
            return allowed_pad
                && GetTickCount64()
                    < m_fake_until_ms.load(std::memory_order_acquire);
        }

        void write_zero_fake_state(XINPUT_STATE* out_state)
        {
            const DWORD packet = m_script_packet.fetch_add(
                1, std::memory_order_acq_rel) + 1;
            *out_state = {};
            out_state->dwPacketNumber = packet;
            const DWORD reads = m_fake_state_reads.fetch_add(
                1, std::memory_order_acq_rel);
            if (reads == 0)
            {
                RC::Output::send<RC::LogLevel::Default>(
                    STR("[GameImGui.XInputHook] fake controller state "
                        "served through XInputGetState\n"));
            }
        }

        void extend_fake_controller_window_until(ULONGLONG until) noexcept
        {
            ULONGLONG current =
                m_fake_until_ms.load(std::memory_order_acquire);
            while (current < until
                   && !m_fake_until_ms.compare_exchange_weak(
                       current,
                       until,
                       std::memory_order_acq_rel,
                       std::memory_order_acquire))
            {
            }
        }

        void log_xinput_caller(const wchar_t* api_name, void* caller) noexcept
        {
            DWORD remaining =
                m_xinput_caller_log_budget.load(std::memory_order_acquire);
            while (remaining > 0)
            {
                if (m_xinput_caller_log_budget.compare_exchange_weak(
                        remaining,
                        remaining - 1,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                    break;
            }
            if (remaining == 0) return;

            wchar_t module_path[MAX_PATH]{};
            HMODULE module = nullptr;
            if (caller && GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                        | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(caller),
                    &module)
                && module)
            {
                (void)GetModuleFileNameW(module, module_path, MAX_PATH);
            }
            RC::Output::send<RC::LogLevel::Default>(
                STR("[GameImGui.XInputHook] caller api={} return=0x{:X} "
                    "module={}\n"),
                api_name ? api_name : L"?",
                reinterpret_cast<uintptr_t>(caller),
                module_path[0] ? module_path : L"?");
        }

        std::unique_ptr<PLH::x64Detour> m_detour;
        std::unique_ptr<PLH::x64Detour> m_capabilities_detour;
        std::unique_ptr<PLH::x64Detour> m_detour_13;
        std::unique_ptr<PLH::x64Detour> m_capabilities_detour_13;
        std::unique_ptr<PLH::x64Detour> m_detour_910;
        std::unique_ptr<PLH::x64Detour> m_capabilities_detour_910;
        std::unique_ptr<PLH::x64Detour> m_native_poller_detour;
        uint64_t                        m_trampoline{0};
        uint64_t                        m_capabilities_trampoline{0};
        uint64_t                        m_trampoline_13{0};
        uint64_t                        m_capabilities_trampoline_13{0};
        uint64_t                        m_trampoline_910{0};
        uint64_t                        m_capabilities_trampoline_910{0};
        uint64_t                        m_native_poller_trampoline{0};
        std::atomic<bool>               m_installed{false};
        std::atomic<bool>               m_native_poller_entry_verified{false};
        std::atomic<bool>               m_native_poller_hook_installed{false};
        std::atomic<uint64_t>           m_native_force_rescan_writes{0};
        std::atomic<uint32_t>           m_hooked_state_api_mask{0};
        std::atomic<uint32_t>           m_hooked_capabilities_api_mask{0};
        std::array<std::atomic<uint64_t>, 3> m_state_api_reads{};
        std::array<std::atomic<uint64_t>, 3> m_capabilities_api_reads{};
        std::atomic<uint8_t>            m_observable_phase{
            static_cast<uint8_t>(ObservablePulsePhase::Idle)};
        std::atomic<bool>               m_connectivity_lease_active{false};
        std::atomic<DWORD>              m_connectivity_lease_pad{0};
        std::atomic<uint8_t>            m_connectivity_lease_api{
            static_cast<uint8_t>(HookedApi::None)};
        std::atomic<uint64_t>           m_connectivity_lease_generation{0};
        std::atomic<uint64_t>
            m_connectivity_lease_generation_counter{0};
        std::atomic<uint64_t>           m_connectivity_lease_state_reads{0};
        std::atomic<uint64_t>
            m_connectivity_lease_forced_state_reads{0};
        std::atomic<uint64_t>
            m_connectivity_lease_capabilities_reads{0};
        std::atomic<uint64_t>
            m_connectivity_lease_forced_capabilities_reads{0};
        std::mutex                      m_connectivity_lease_control_mutex;
        std::mutex                      m_observable_pulse_mutex;
        ObservablePulseStatus          m_observable_pulse{};
        uint64_t                        m_observable_generation_counter{0};
        uint64_t                        m_last_neutral_ack_generation{0};
        uint64_t                        m_last_press_ack_generation{0};
        uint64_t                        m_last_release_ack_generation{0};
        uint64_t                        m_last_fake_state_ack_generation{0};
        uint64_t                        m_last_fake_capabilities_ack_generation{0};
        uint64_t                        m_last_completion_generation{0};
        uint64_t                        m_last_failure_generation{0};
        uint64_t                        m_last_reset_generation{0};
        std::atomic<bool>               m_script_active{false};
        std::atomic<DWORD>              m_script_packet{1};
        std::atomic<DWORD>              m_script_state_reads{0};
        std::atomic<DWORD>              m_script_capability_reads{0};
        std::atomic<DWORD>              m_fake_state_reads{0};
        std::atomic<DWORD>              m_xinput_caller_log_budget{0};
        std::atomic<bool>               m_fake_all_pads{false};
        std::mutex                      m_script_mutex;
        std::vector<ScriptStep>         m_script;
        ULONGLONG                       m_script_start_ms{0};
        std::atomic<ULONGLONG>          m_fake_until_ms{0};
    };
}
