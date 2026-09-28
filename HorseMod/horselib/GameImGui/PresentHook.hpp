// ============================================================================
// Horse::GameImGui::PresentHook — polite DXGI Present detour.
//
// What this does
// --------------
// Hooks IDXGISwapChain::Present and ::ResizeBuffers on the game's real
// swap chain via PolyHook 2.0's VFuncSwapHook.  This is a VTABLE swap,
// NOT a code-patch detour — PolyHook replaces vtable[Present] and
// vtable[ResizeBuffers] with trampolines that point at our callbacks;
// the original function pointers are preserved as the trampolines.
//
// Why vtable swap specifically
// ----------------------------
// Code-patch detours (MinHook, Detours) on these DXGI functions fight
// with Steam's overlay (gameoverlayrenderer64.dll), which also patches
// the same DXGI code when its ImGui render step loads.  VFuncSwapHook
// operates on the COM vtable pointer stored in the game's swap chain
// object itself, which is orthogonal to any code-patch hook either we
// or Steam might install on the underlying function bodies.  Steam's
// input and overlay logic use the code-patched path; we use the
// vtable-swap path; both work.
//
// Acquiring the game's swap chain vtable
// --------------------------------------
// We don't know the game's swap chain pointer at DLL-load time — UE4
// constructs it during engine init.  Two viable approaches:
//
//   A. Create a throwaway swap chain via D3D11CreateDeviceAndSwapChain
//      with a 1×1 message-only window, read its vtable, PolyHook-swap
//      that vtable.  Simpler but patches the shared system vtable
//      (rare but safe).
//
//   B. Wait for the game's own swap chain, grab it via D3D11On12 /
//      ScopedExperimentalMode / just instrumenting the first frame.
//
// We use approach A: it's robust, only runs once at init, and because
// DXGI vtables are per-process shared pointers, our swap hook applies
// to the game's swap chain as well as any other in-process DXGI
// swap chain (there shouldn't be others).
//
// Install timing
// --------------
// install() must NOT be called from DllMain.  Steam's overlay hook is
// installed lazily during Steam's own initialisation, typically after
// the game's first D3D device creation.  Installing our hook too early
// risks being overwritten by Steam; installing too late is fine
// (PolyHook re-reads vtable on install).  In practice we call install()
// from the first game-thread tick (HorseMod's cockpit pre-hook).
//
// Thread-safety
// -------------
// install() / uninstall() are main-thread / game-thread only (serialize
// them via the mod lifecycle).  The hook callbacks themselves run on
// whatever thread the game uses for presentation. Replay presentation records
// that actual thread and rejects work submitted from another one.
// ============================================================================

#pragma once

#include "DX11State.hpp"
#include "GamepadInput.hpp"
#include "ReplayTickEntry.hpp"

#include <polyhook2/Virtuals/VFuncSwapHook.hpp>

#include <DynamicOutput/DynamicOutput.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Horse::GameImGui
{
    using Microsoft::WRL::ComPtr;

    // ------------------------------------------------------------------
    // IDXGISwapChain vtable indices (documented DXGI ABI, stable since
    // Windows 7).  Subject to change only if Microsoft ships a new
    // DXGI interface revision in an OS update, which hasn't happened
    // in 15+ years.  Typed uint16_t to match PolyHook's VFuncMap key.
    // ------------------------------------------------------------------
    constexpr uint16_t kIDXGISwapChain_Present        = 8;
    constexpr uint16_t kIDXGISwapChain_ResizeBuffers  = 13;

    // ------------------------------------------------------------------
    // Frame callback type — invoked once per Present between NewFrame
    // and Render.  Register via GameImGui::register_tab.
    // ------------------------------------------------------------------
    using FrameCallback = std::function<void()>;
    using PassiveDrawCallback = std::function<bool()>;

    class PresentHook
    {
    public:
        static PresentHook& instance()
        {
            static PresentHook s;
            return s;
        }
        using ReplayOutputObserver = void (*)(void*, IDXGISwapChain*, std::uint64_t);
        bool set_replay_output_observer(void* owner, ReplayOutputObserver callback)
        {
            const auto prior=m_replay_output_observer.load();
            if(prior && prior->owner!=owner) return false;
            if(callback) {
                if(!owner) return false;
                try {m_replay_output_observer.store(std::make_shared<ReplayOutputBinding>(ReplayOutputBinding{owner,callback}));}
                catch(...) {return false;}
            } else m_replay_output_observer.store(nullptr);
            return true;
        }

        // Install the vtable hook.  Returns true on success.  Safe to
        // call exactly once per mod lifetime; subsequent calls are
        // no-ops.
        bool install();

        // Uninstall on mod teardown.  PolyHook restores the original
        // vtable entries.  Safe even if install never succeeded.
        void uninstall();

        // Register a per-frame ImGui callback.  Called between
        // ImGui::NewFrame() and ImGui::Render() in Present order of
        // registration.  Returns a token used for unregister().
        uint64_t register_frame_callback(FrameCallback cb);
        void     unregister_frame_callback(uint64_t token);

        // Register a passive, non-interactive draw callback.  Passive
        // callbacks may render while the main overlay is hidden, but they
        // must not depend on input.  Return true while another passive frame
        // is needed.
        uint64_t register_passive_draw_callback(PassiveDrawCallback cb);
        void     unregister_passive_draw_callback(uint64_t token);
        void     request_passive_draw(bool requested = true) noexcept
        {
            if (requested)
            {
                m_passive_draw_generation.fetch_add(
                    1, std::memory_order_acq_rel);
            }
            m_passive_draw_requested.store(requested,
                                           std::memory_order_release);
        }

        // Set a callback invoked on the rising edge of the gamepad
        // BACK (Select / View) button.  Used by GameImGui::initialize
        // to wire Select → visibility toggle.  Pass nullptr / empty
        // std::function to disable.  Safe to call any time; the next
        // Present picks up the new value.
        void set_on_gamepad_back(std::function<void()> cb)
        {
            m_on_gamepad_back = std::move(cb);
        }

        // For the WndProc hook to ask "is an ImGui overlay currently
        // capturing input?"  Read from the hook thread safely.
        bool imgui_wants_mouse()    const noexcept;
        bool imgui_wants_keyboard() const noexcept;

        // Debug counter for the tab to show liveness.
        uint64_t present_count() const noexcept
        {
            return m_present_count.load(std::memory_order_relaxed);
        }

        // Render-thread-only surface lease for an unfinished replay world.
        // Arm before the target so a normal frame can be copied before ImGui.
        // These calls never tick a viewport, world, Slate widget, or actor.
        bool arm_replay_surface(std::uint64_t budget_bytes);
        bool draw_replay_surface();
        struct ReplaySurfaceImage;
        bool publish_replay_surface(const ReplaySurfaceImage* expected=nullptr);
        bool finish_replay_display_transaction(bool commit);
        bool prepare_replay_output(std::uint64_t budget_bytes);
        ID3D11Texture2D* replay_draw_image() const noexcept;
        bool stage_replay_native_draw();
        bool restore_replay_native_backbuffer();
        HRESULT present_replay_frame(IDXGISwapChain* swap_chain,UINT sync_interval,UINT flags);
        bool release_replay_output();
        bool release_replay_surface(bool transfer_to_advance = false);
        bool retire_replay_surface();
        bool replay_retirement_pending() const noexcept { return m_replay_native_pending.load(); }
        struct ReplaySurfaceImage
        {
            ComPtr<ID3D11Texture2D> image;
            ComPtr<ID3D11Device> device;
            std::uint64_t bytes{};
            std::uint64_t source_present{};
        };
        // Retain the target's pre-overlay image. Retention is not a GPU
        // completion witness; the enclosing ordered publication owns that.
        bool retain_replay_surface(ReplaySurfaceImage& output);
        bool install_replay_surface(const ReplaySurfaceImage& target, ReplaySurfaceImage& undo, bool hold_native_display=false);
        bool undo_replay_surface(const ReplaySurfaceImage& target, ReplaySurfaceImage& undo);
        std::uint64_t replay_surface_bytes() const noexcept
        {
            const auto image = m_replay_bytes.load();
            // Output resources and their UI thread are admitted before capture.
            return image + m_replay_scratch_bytes.load() + m_replay_transfer_bytes.load();
        }
        std::uint64_t replay_surface_frames() const noexcept { return m_replay_frames.load(); }
        bool replay_surface_ready() const noexcept { return m_replay_ready.load(); }
        std::uint64_t replay_resume_requests() const noexcept { return m_replay_resume_requests.load(); }
        std::uint64_t replay_step_requests() const noexcept { return m_replay_step_requests.load(); }
        std::uint64_t replay_cancel_requests() const noexcept { return m_replay_cancel_requests.load(); }
        void publish_replay_controls(std::uint64_t tick, bool can_step, bool can_cancel, bool failed) noexcept
        { m_replay_tick.store(tick); m_replay_can_step.store(can_step); m_replay_can_cancel.store(can_cancel); m_replay_seek_failed.store(failed); }
        static constexpr std::uint64_t no_seek_request=UINT64_MAX;
        static constexpr std::uint64_t previous_round_request=UINT64_MAX-1;
        static constexpr std::uint64_t next_round_request=UINT64_MAX-2;
        static constexpr std::uint64_t exit_replay_request=UINT64_MAX-3;
        std::uint64_t take_replay_seek_request() noexcept {return m_replay_seek_request.exchange(no_seek_request);}
        void publish_replay_timeline(bool complete,std::uint64_t last_tick,unsigned failure,
            std::uint64_t first_checkpoint=UINT64_MAX,int round=-1,unsigned round_tick=0,bool unsupported_tail=false,bool can_exit=false) noexcept {
            m_replay_last_tick.store(last_tick);m_replay_index_failure.store(failure);
            m_replay_first_checkpoint.store(first_checkpoint);m_replay_round.store(round);m_replay_round_tick.store(round_tick);
            m_replay_index_complete.store(complete);
            m_replay_unsupported_tail.store(unsupported_tail);m_replay_can_exit.store(can_exit);
        }
        void publish_replay_index_progress(unsigned phase,std::uint64_t tick,unsigned failure,bool can_cancel) noexcept {
            m_replay_index_observed.store(tick);m_replay_progress_failure.store(failure);
            m_replay_index_can_cancel.store(can_cancel);m_replay_progress_phase.store(phase);
        }
        bool take_replay_index_cancel() noexcept {return m_replay_index_cancel.exchange(false);}
        bool take_replay_pause_request() noexcept {return m_replay_pause_request.exchange(false);}
        void publish_replay_pause_availability(bool allowed,std::uint64_t tick) noexcept {
            m_replay_can_pause.store(allowed);m_replay_tick.store(tick);
        }
        void report_replay_pause_request(bool accepted) noexcept {
            m_replay_pause_rejected.store(!accepted);if(accepted)m_replay_can_pause.store(false);
        }
        static constexpr std::size_t replay_timeline_control_bytes() noexcept {return 128+sizeof(ReplayTickEntry);}
        bool consumed_replay_mouse_message(DWORD message_time) const noexcept;

    private:
        // Initialize m_callbacks with an empty-but-non-null vector so
        // register_frame_callback / unregister_frame_callback are safe
        // to call even if install() was never run (e.g. when the
        // disable_gameimgui.txt kill switch triggers and
        // GameImGui::initialize returns early).  Without this, the
        // first register_tab after a skipped-install dereferences a
        // null shared_ptr and crashes.
        PresentHook()
        {
            m_callbacks.store(
                std::make_shared<const std::vector<CallbackEntry>>());
            m_passive_callbacks.store(
                std::make_shared<const std::vector<PassiveCallbackEntry>>());
        }
        ~PresentHook() { uninstall(); }
        PresentHook(const PresentHook&)            = delete;
        PresentHook& operator=(const PresentHook&) = delete;

        // -------- Hook callbacks (static; dispatched to instance) ----

        using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

        static HRESULT STDMETHODCALLTYPE Present_detour(IDXGISwapChain* swap_chain,
                                                        UINT sync_interval,
                                                        UINT flags);
        static HRESULT STDMETHODCALLTYPE ResizeBuffers_detour(IDXGISwapChain* swap_chain,
                                                              UINT buffer_count,
                                                              UINT width,
                                                              UINT height,
                                                              DXGI_FORMAT new_format,
                                                              UINT swap_chain_flags);

        // -------- Body of the Present callback (runs on render
        //          thread).  Draws ImGui into the back buffer, chains
        //          to original Present.  Never throws; any failure
        //          degrades to "just chain through". ----------------

        HRESULT on_present(IDXGISwapChain* swap_chain,
                           UINT sync_interval,
                           UINT flags);

        // -------- Create a throwaway swap chain so PolyHook has a
        //          live COM object whose vtable it can edit.  DXGI
        //          vtables are process-wide shared pointers, so our
        //          edits apply to the game's swap chain as well.  We
        //          keep the object alive for the lifetime of the
        //          hook; PolyHook's VFuncSwapHook restores the vtable
        //          in its destructor.  ----------------------------

        bool create_probe_swap_chain();
        void destroy_probe_swap_chain();

        // -------- State ---------------------------------------------

        // Original function pointers (captured from vtable before we
        // overwrite the slots).  Used as the trampolines.
        Present_t        m_original_present        = nullptr;
        ResizeBuffers_t  m_original_resize_buffers = nullptr;

        // The "originals" map VFuncSwapHook captures the pre-hook vtable
        // pointers into.  CRITICAL: VFuncSwapHook stores a POINTER to
        // this map (its m_userOrigMap) and dereferences it again in
        // unHook() - which runs from ~VFuncSwapHook.  So the map MUST
        // outlive m_vfunc_hook.  It is a member (not an install()-local)
        // for exactly that reason, and is declared BEFORE m_vfunc_hook so
        // it destructs AFTER it.
        //
        // A prior stack-local `originals` in install() was a use-after-
        // scope: install() returned, the map was destroyed, and the
        // later ~VFuncSwapHook -> unHook() did `for (auto& p : *m_userOrigMap)`
        // over freed stack memory - faulting on a garbage std::map node
        // (`MOV RAX,[RAX]`, value 0x1).  That is the long-standing crash
        // seen in the seek/teardown minidumps.
        PLH::VFuncMap m_vfunc_originals;

        // PolyHook owns the vtable-swap state.  We only need one
        // instance covering both Present and ResizeBuffers.
        std::unique_ptr<PLH::VFuncSwapHook> m_vfunc_hook;

        // The live swap chain + supporting objects PolyHook holds a
        // reference to via the `Class` constructor argument.  Kept
        // alive as long as the hook is active.
        ComPtr<ID3D11Device>        m_probe_device;
        ComPtr<ID3D11DeviceContext> m_probe_context;
        ComPtr<IDXGISwapChain>      m_probe_swap_chain;
        HWND                        m_probe_hwnd      {nullptr};
        const wchar_t*              m_probe_class_name{L"HorseGameImGuiVTProbe"};

        std::atomic<bool>     m_installed{false};

        // Per-frame callbacks.  Modified only from install-thread at
        // register time, read from render thread during Present.
        // Using a shared_ptr to a const vector so we can swap it
        // atomically under a mutex-free reader.
        struct CallbackEntry { uint64_t token; FrameCallback cb; };
        struct PassiveCallbackEntry
        {
            uint64_t token;
            PassiveDrawCallback cb;
        };
        mutable std::atomic<std::shared_ptr<const std::vector<CallbackEntry>>> m_callbacks;
        mutable std::atomic<std::shared_ptr<const std::vector<PassiveCallbackEntry>>> m_passive_callbacks;
        std::atomic<uint64_t> m_next_token{1};
        std::atomic<bool>     m_passive_draw_requested{false};
        std::atomic<uint64_t> m_passive_draw_generation{0};

        // Rising-edge callback for gamepad BACK button.  Written by
        // set_on_gamepad_back (from init thread), read by the render
        // thread via GamepadInput::feed_to_imgui's edge detector.
        // std::function is not atomic, but we only ever publish it
        // ONCE at init and never change it thereafter — the render
        // thread's read happens after init completes, so no races.
        std::function<void()> m_on_gamepad_back;

        std::atomic<uint64_t> m_present_count{0};
        std::atomic<uint64_t> m_reentrant_present_count{0};
        std::atomic<DWORD> m_present_thread{};
        ComPtr<IDXGISwapChain> m_game_swap_chain;
        ComPtr<ID3D11Texture2D> m_replay_image,m_replay_display_hold_image;
        // Original viewport output; scratch restores native backbuffer contents
        // before the GPU completion receipt permits native continuation.
        struct ReplayOutputBinding {void* owner{};ReplayOutputObserver callback{};};
        std::atomic<std::shared_ptr<ReplayOutputBinding>> m_replay_output_observer;
        HWND m_replay_output_window{};
        ComPtr<IDXGISwapChain> m_replay_output_chain;
        ComPtr<ID3D11Texture2D> m_replay_output_image;
        ComPtr<ID3D11RenderTargetView> m_replay_output_rtv;
        ComPtr<ID3D11Texture2D> m_replay_backbuffer_undo;
        bool m_replay_display_transaction{},m_replay_display_publishing{},m_replay_display_published{};
        std::atomic<std::uint64_t> m_replay_suppressed_presents{};
        ComPtr<ID3D11Texture2D> m_replay_native_target;
        ComPtr<ID3D11Query> m_replay_native_completion;
        ComPtr<ID3D11DeviceContext> m_replay_native_context;
        std::atomic<std::uint64_t> m_replay_scratch_bytes{}, m_replay_transfer_bytes{};
        std::atomic<bool> m_replay_native_pending{};
        std::uint64_t m_replay_source_present{};
        std::atomic<std::uint64_t> m_replay_bytes{}, m_replay_frames{};
        std::atomic<bool> m_replay_ready{};
        std::atomic<std::uint64_t> m_replay_resume_requests{};
        std::atomic<std::uint64_t> m_replay_step_requests{}, m_replay_tick{};
        std::atomic<std::uint64_t> m_replay_cancel_requests{};
        // Render-side value mailbox; no native game pointers cross threads.
        std::atomic<std::uint64_t> m_replay_seek_request{no_seek_request},m_replay_last_tick{};
        std::atomic<bool> m_replay_unsupported_tail{},m_replay_can_exit{};
        std::atomic<bool> m_replay_index_complete{};
        std::atomic<unsigned> m_replay_index_failure{};
        std::atomic<std::uint64_t> m_replay_first_checkpoint{UINT64_MAX};
        std::atomic<int> m_replay_round{-1};
        std::atomic<unsigned> m_replay_round_tick{};
        std::atomic<std::uint64_t> m_replay_index_observed{};
        std::atomic<unsigned> m_replay_progress_phase{},m_replay_progress_failure{};
        std::atomic<bool> m_replay_index_can_cancel{},m_replay_index_cancel{};
        std::uint64_t m_replay_tick_entry{};
        std::atomic<bool> m_replay_can_cancel{}, m_replay_seek_failed{};
        std::atomic<bool> m_replay_can_step{};
        std::atomic<DWORD> m_replay_mouse_begin{}, m_replay_mouse_end{};
        std::atomic<bool> m_replay_mouse_active{}, m_replay_mouse_seen{};
        bool m_replay_controls_reported{};
        bool m_replay_step_reported{};
        bool m_replay_cancel_reported{};
        std::uint64_t m_replay_budget{};
        bool m_replay_armed{}, m_replay_drawing{}, m_replay_capture_reported{}, m_replay_transfer{};
        ReplayTickEntry m_replay_numeric_entry;
        std::atomic<bool> m_replay_pause_request{},m_replay_can_pause{},m_replay_pause_rejected{};
        bool m_replay_pause_control_reported{},m_replay_numeric_open_reported{},m_replay_exit_reported{};
        unsigned m_replay_numeric_frames{};
        UINT m_game_sync_interval{}, m_game_present_flags{};
        void capture_replay_surface(IDXGISwapChain* swap_chain);
    };

    // ------------------------------------------------------------------
    // Inline implementation.  Kept in the header to match the rest of
    // horselib's style and avoid adding a .cpp to HorseMod's CMake.
    // ------------------------------------------------------------------

    inline bool PresentHook::install()
    {
        if (m_installed.load(std::memory_order_acquire))
        {
            return true;
        }

        // Keep callbacks registered before deferred install. HorseMod
        // registers its tab and passive toast draw callback during
        // on_unreal_init(), then installs this hook a few game-thread ticks
        // later. Replacing these snapshots here silently drops the UI.
        if (!m_callbacks.load(std::memory_order_acquire))
        {
            m_callbacks.store(
                std::make_shared<const std::vector<CallbackEntry>>());
        }
        if (!m_passive_callbacks.load(std::memory_order_acquire))
        {
            m_passive_callbacks.store(
                std::make_shared<const std::vector<PassiveCallbackEntry>>());
        }

        if (!create_probe_swap_chain() || !m_probe_swap_chain)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[GameImGui] failed to create DXGI probe swap chain\n"));
            return false;
        }

        // Pre-read the vtable ourselves before PolyHook overwrites
        // it.  `*obj` on a COM object IS the vtable pointer; vtable[i]
        // is the i'th function pointer.
        void** vtable = *reinterpret_cast<void***>(m_probe_swap_chain.Get());
        m_original_present        = reinterpret_cast<Present_t>(vtable[kIDXGISwapChain_Present]);
        m_original_resize_buffers = reinterpret_cast<ResizeBuffers_t>(vtable[kIDXGISwapChain_ResizeBuffers]);

        // PolyHook's VFuncSwapHook takes an OBJECT pointer (not a
        // vtable pointer) — it reads *object internally to find the
        // vtable, then writes the new function pointers into vtable
        // slots.  DXGI vtables are process-wide shared pointers, so
        // our edit applies to the game's swap chain too.
        PLH::VFuncMap redirects;
        redirects[kIDXGISwapChain_Present]       = reinterpret_cast<uint64_t>(&PresentHook::Present_detour);
        redirects[kIDXGISwapChain_ResizeBuffers] = reinterpret_cast<uint64_t>(&PresentHook::ResizeBuffers_detour);

        // PolyHook writes the old function pointers into this map after
        // hook(), and RETAINS the pointer - it iterates the map again in
        // unHook() from ~VFuncSwapHook.  It must therefore be the member
        // m_vfunc_originals (which outlives m_vfunc_hook), never an
        // install()-local.  Cleared first in case of a re-install.
        m_vfunc_originals.clear();
        m_vfunc_hook = std::make_unique<PLH::VFuncSwapHook>(
            reinterpret_cast<uint64_t>(m_probe_swap_chain.Get()),
            redirects,
            &m_vfunc_originals);
        if (!m_vfunc_hook->hook())
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[GameImGui] VFuncSwapHook::hook() failed\n"));
            m_vfunc_hook.reset();
            destroy_probe_swap_chain();
            return false;
        }

        // Prefer PolyHook's captured originals over our pre-read copy
        // (defensive — in case the vtable was modified between the
        // read and the hook).
        if (auto it = m_vfunc_originals.find(kIDXGISwapChain_Present);
            it != m_vfunc_originals.end() && it->second)
        {
            m_original_present = reinterpret_cast<Present_t>(it->second);
        }
        if (auto it = m_vfunc_originals.find(kIDXGISwapChain_ResizeBuffers);
            it != m_vfunc_originals.end() && it->second)
        {
            m_original_resize_buffers = reinterpret_cast<ResizeBuffers_t>(it->second);
        }

        m_installed.store(true, std::memory_order_release);
        const auto callbacks = m_callbacks.load(std::memory_order_acquire);
        const auto passive_callbacks =
            m_passive_callbacks.load(std::memory_order_acquire);
        RC::Output::send<RC::LogLevel::Default>(
            STR("[GameImGui] PresentHook installed (orig Present={} "
                "orig Resize={} callbacks={} passive_callbacks={})\n"),
            reinterpret_cast<uintptr_t>(m_original_present),
            reinterpret_cast<uintptr_t>(m_original_resize_buffers),
            callbacks ? callbacks->size() : 0,
            passive_callbacks ? passive_callbacks->size() : 0);
        return true;
    }

    // SEH-wrapped VFuncSwapHook teardown — must be a free function (no
    // C++ unwinding frames in scope) because __try/__except can't sit
    // alongside C++ destructors.  Returns true on a clean teardown,
    // false if it faulted (eaten — we're shutting down anyway).
    //
    // Why this is wrapped:
    //   ~VFuncSwapHook -> unHook() writes the saved original pointers
    //   back into the DXGI swap-chain vtable.  During an UNCLEAN
    //   shutdown (the game crashed and DXGI was already torn down by
    //   its crash handler) that vtable memory has been decommitted, so
    //   the write faults.  Catch it: the OS reclaims everything in a
    //   moment anyway and an un-restored vtable is harmless then.
    //
    //   On a fault the hook is RELEASED (deliberately leaked) rather
    //   than destructed: if reset() faulted mid-unHook the object is
    //   left with PolyHook's m_hooked still true, and letting its
    //   destructor run again would re-invoke the faulting unHook()
    //   UNGUARDED — a second-chance AV that replaces the ORIGINAL crash
    //   in the minidump and hides the real bug.  Leaking it at process
    //   teardown costs nothing.
    //
    //   The other historical fault here — unHook() iterating a freed
    //   std::map (`MOV RAX,[RAX]`, value 0x1) — was a use-after-scope:
    //   install() passed a stack-local `originals` map by address and
    //   VFuncSwapHook kept the pointer.  Fixed by making it the
    //   m_vfunc_originals member; this guard now only covers the
    //   genuinely-decommitted-vtable case.
    static bool seh_teardown_vfunc_hook(
        std::unique_ptr<PLH::VFuncSwapHook>& hook) noexcept
    {
        __try
        {
            hook.reset();   // ~VFuncSwapHook runs unHook() if m_hooked
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            hook.release(); // abandon unfreed so the dtor can't re-fault
            return false;
        }
    }

    inline void PresentHook::uninstall()
    {
        if(m_replay_native_pending || m_replay_drawing) {
            RC::Output::send<RC::LogLevel::Error>(STR("[GameImGui] native viewport retirement pending; hook retained\n"));
            return;
        }
        if (!m_installed.exchange(false)) return;
        if (m_vfunc_hook)
        {
            if (!seh_teardown_vfunc_hook(m_vfunc_hook))
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui] VFuncSwapHook teardown faulted "
                        "(DXGI already torn down?) — swallowed; the "
                        "hook was leaked so its destructor cannot "
                        "re-fault.  OS will reclaim it.\n"));
            }
        }
        m_original_present        = nullptr;
        m_original_resize_buffers = nullptr;
        destroy_probe_swap_chain();
    }

    inline uint64_t PresentHook::register_frame_callback(FrameCallback cb)
    {
        const uint64_t token = m_next_token.fetch_add(1, std::memory_order_relaxed);
        auto current = m_callbacks.load();
        auto next = std::make_shared<std::vector<CallbackEntry>>(*current);
        next->push_back({token, std::move(cb)});
        m_callbacks.store(std::shared_ptr<const std::vector<CallbackEntry>>(next));
        return token;
    }

    inline void PresentHook::unregister_frame_callback(uint64_t token)
    {
        auto current = m_callbacks.load();
        auto next = std::make_shared<std::vector<CallbackEntry>>();
        next->reserve(current->size());
        for (const auto& e : *current)
        {
            if (e.token != token) next->push_back(e);
        }
        m_callbacks.store(std::shared_ptr<const std::vector<CallbackEntry>>(next));
    }

    inline uint64_t PresentHook::register_passive_draw_callback(
        PassiveDrawCallback cb)
    {
        const uint64_t token = m_next_token.fetch_add(1,
            std::memory_order_relaxed);
        auto current = m_passive_callbacks.load();
        auto next =
            std::make_shared<std::vector<PassiveCallbackEntry>>(*current);
        next->push_back({token, std::move(cb)});
        m_passive_callbacks.store(
            std::shared_ptr<const std::vector<PassiveCallbackEntry>>(next));
        return token;
    }

    inline void PresentHook::unregister_passive_draw_callback(uint64_t token)
    {
        auto current = m_passive_callbacks.load();
        auto next = std::make_shared<std::vector<PassiveCallbackEntry>>();
        next->reserve(current->size());
        for (const auto& e : *current)
        {
            if (e.token != token) next->push_back(e);
        }
        m_passive_callbacks.store(
            std::shared_ptr<const std::vector<PassiveCallbackEntry>>(next));
        if (next->empty())
        {
            m_passive_draw_requested.store(false, std::memory_order_release);
        }
    }

    // -----------------------------------------------------------------
    // Context-safe IO query.  WndProc runs on the UI thread while the
    // render thread is inside Present (possibly flipping the current
    // ImGui context).  ImGui::GetIO() reads the *current* context, so
    // we save/restore around the read to guarantee we query ours.
    // -----------------------------------------------------------------
    namespace detail
    {
        inline thread_local int g_present_depth = 0;

        struct PresentDepthScope
        {
            PresentDepthScope() noexcept { ++g_present_depth; }
            ~PresentDepthScope() noexcept { --g_present_depth; }
            PresentDepthScope(const PresentDepthScope&) = delete;
            PresentDepthScope& operator=(const PresentDepthScope&) = delete;
        };

        inline const ImGuiIO* scoped_io_read(ImGuiContext* target)
        {
            if (!target) return nullptr;
            ImGuiContext* prev = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(target);
            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetCurrentContext(prev);
            return &io;
        }
    }

    inline bool PresentHook::imgui_wants_mouse() const noexcept
    {
        auto& state = DX11State::instance();
        if (!state.ready()) return false;
        const ImGuiIO* io = detail::scoped_io_read(state.imgui_context());
        return io && io->WantCaptureMouse;
    }

    inline bool PresentHook::imgui_wants_keyboard() const noexcept
    {
        auto& state = DX11State::instance();
        if (!state.ready()) return false;
        const ImGuiIO* io = detail::scoped_io_read(state.imgui_context());
        return io && io->WantCaptureKeyboard;
    }

    // -------- Static thunk → instance --------

    inline HRESULT STDMETHODCALLTYPE PresentHook::Present_detour(IDXGISwapChain* swap_chain,
                                                                  UINT sync_interval,
                                                                  UINT flags)
    {
        return instance().on_present(swap_chain, sync_interval, flags);
    }

    inline HRESULT STDMETHODCALLTYPE PresentHook::ResizeBuffers_detour(IDXGISwapChain* swap_chain,
                                                                        UINT buffer_count,
                                                                        UINT width,
                                                                        UINT height,
                                                                        DXGI_FORMAT new_format,
                                                                        UINT swap_chain_flags)
    {
        auto& self = instance();
        if(swap_chain==self.m_game_swap_chain.Get() && self.m_replay_scratch_bytes.load()) {
            // The retained transaction still owns this buffer. Do not partially
            // tear down its bindings or resize underneath an in-flight copy.
            self.m_replay_ready.store(false);
            return DXGI_ERROR_INVALID_CALL;
        }
        // Release our back-buffer RTV BEFORE chaining; otherwise the
        // resize fails with E_INVALIDARG because we hold a reference
        // to buffer 0.
        DX11State::instance().release_rtv();
        // ImGui DX11 backend also holds references to the old back
        // buffer textures via the internal resources it creates per
        // device — we invalidate those by invalidating device objects.
        if (DX11State::instance().ready())
        {
            ImGui_ImplDX11_InvalidateDeviceObjects();
        }
        self.m_replay_ready.store(false);
        // Resize may originate on the game thread. The presentation owner
        // releases/replaces its texture on the recorded render thread only.
        HRESULT hr = self.m_original_resize_buffers
            ? self.m_original_resize_buffers(swap_chain, buffer_count,
                                             width, height, new_format,
                                             swap_chain_flags)
            : S_OK;
        // We do NOT rebuild the RTV here; the next Present will do it
        // via DX11State::ensure_rtv_after_resize.
        return hr;
    }

    // -------- Body of the Present detour --------

    inline HRESULT PresentHook::on_present(IDXGISwapChain* swap_chain,
                                           UINT sync_interval,
                                           UINT flags)
    {
        m_present_count.fetch_add(1, std::memory_order_relaxed);

        // Trampoline pointer captured.  If missing (shouldn't happen
        // post-install), fall through and let the swap chain call its
        // own vtable — but we've already overwritten the slot.  Worst
        // case: no Present happens this frame.  Log and return.
        if (!m_original_present)
        {
            RC::Output::send<RC::LogLevel::Error>(
                STR("[GameImGui] Present_detour with no trampoline?!\n"));
            return DXGI_ERROR_INVALID_CALL;
        }

        if (detail::g_present_depth > 0)
        {
            // Steam's overlay can call Present through the swap-chain vtable
            // while it is already inside the DXGI Present body we chained to.
            // Since our vtable slot points back here, chaining again would
            // recurse HorseMod -> Steam -> HorseMod until stack overflow.
            const uint64_t n = m_reentrant_present_count.fetch_add(
                1, std::memory_order_relaxed) + 1;
            if (n <= 5 || (n % 300) == 0)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui] nested Present detected; returning "
                        "S_OK to break Steam overlay recursion (count={})\n"),
                    n);
            }
            return S_OK;
        }

        detail::PresentDepthScope present_depth_scope;

        DWORD unbound = 0;
        m_present_thread.compare_exchange_strong(unbound, GetCurrentThreadId());

        auto& state = DX11State::instance();

        // Lazy init on first hooked Present — we NOW have the real
        // swap chain and thus the game's device/context/HWND.
        bool init_ok = state.ensure_initialised(swap_chain);

        if (init_ok && m_present_thread.load() == GetCurrentThreadId()
            && swap_chain != m_probe_swap_chain.Get()
            && !(flags & DXGI_PRESENT_TEST))
        {
            DXGI_SWAP_CHAIN_DESC description{};
            if (SUCCEEDED(swap_chain->GetDesc(&description)) && description.OutputWindow == state.hwnd())
            {
                m_game_swap_chain = swap_chain;
                if(!m_replay_drawing) {
                    m_game_sync_interval = sync_interval;
                    m_game_present_flags = flags;
                }
                if (m_replay_armed && !m_replay_drawing) capture_replay_surface(swap_chain);
            }
        }

        // Rebuild RTV if a recent ResizeBuffers dropped it.
        if (init_ok)
        {
            init_ok = state.ensure_rtv_after_resize(swap_chain);
        }

        // --- Gamepad BACK-button edge detection ---
        // Poll every frame regardless of overlay visibility, so Select
        // can reopen the overlay from hidden state.
        //
        // A previous version of this code gated the poll on visibility
        // because we suspected a second XInput caller was confusing
        // Steam Input's PS4 emulation.  It turned out the ACTUAL cause
        // was the d3dcompiler_47.dll import triggering Steam Input's
        // defensive response at DLL-load time — not our per-frame
        // XInput calls.  The fix is the /DELAYLOAD linker flags in
        // HorseMod/CMakeLists.txt, which stop d3dcompiler_47 from
        // being pulled into the process unless we actually use it.
        //
        // With delay-load in place, concurrent XInput reads are fine.
        static const std::function<void()> no_toggle;
        GamepadInput::instance().poll_and_detect_back(m_replay_drawing ? no_toggle : m_on_gamepad_back);

        const bool visible =
            g_overlay_visible.load(std::memory_order_relaxed);
        const bool passive_requested =
            m_passive_draw_requested.load(std::memory_order_acquire);
        const uint64_t passive_generation =
            m_passive_draw_generation.load(std::memory_order_acquire);

        if ((visible || passive_requested) && init_ok
            && state.imgui_context())
        {
            // Bind OUR context before any ImGui:: call.  UE4SS might
            // have set its own context via UE4SS_ENABLE_IMGUI(), and
            // tab callbacks will run ImGui:: functions.  They must
            // target our context, not UE4SS's.
            state.bind_imgui_context();
            ImGuiIO& io = ImGui::GetIO();
            const ImGuiConfigFlags saved_config_flags = io.ConfigFlags;
            if (!visible)
            {
                io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            }

            // Feed the cached gamepad state into ImGui for nav.  We
            // only call this when the frame is actually going to
            // NewFrame / Render — otherwise the queued AddKeyEvent
            // calls would pile up in a context that never processes
            // them (leak).
            if (visible)
            {
                GamepadInput::instance().feed_nav_to_imgui(
                    state.imgui_context());
            }

            // --- ImGui frame ---
            const auto replay_control_window=m_replay_drawing?m_replay_output_window:state.hwnd();
            if (m_replay_drawing)
            {
                // Poll physical state without entering the suspended game
                // thread's message queue. Only this held replay view consumes
                // these buttons; queued copies are filtered on native resume.
                const bool focused = GetForegroundWindow() == GetAncestor(replay_control_window, GA_ROOT);
                io.AddFocusEvent(focused);
                const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
                // Win32's backend polls position only when MouseTrackedArea
                // is zero. A held application cannot deliver WM_MOUSEMOVE to
                // update an already tracked cursor; publish its current client
                // position before the physical button transitions as well.
                POINT cursor{};
                if(focused && GetCursorPos(&cursor) && ScreenToClient(replay_control_window,&cursor))
                    io.AddMousePosEvent(static_cast<float>(cursor.x),static_cast<float>(cursor.y));
                io.AddMouseButtonEvent(0, focused && (GetAsyncKeyState(swapped ? VK_RBUTTON : VK_LBUTTON) & 0x8000));
                io.AddMouseButtonEvent(1, focused && (GetAsyncKeyState(swapped ? VK_LBUTTON : VK_RBUTTON) & 0x8000));
            }
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            bool focus_replay_playback=false;
            const auto report_replay_button=[&](const char* name) {
                const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
                POINT center{static_cast<LONG>((low.x+high.x)*0.5f),static_cast<LONG>((low.y+high.y)*0.5f)};
                if(ClientToScreen(replay_control_window,&center))
                    RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] replay button name={} hwnd={} screen_x={} screen_y={} tick={}\n"),
                        RC::to_generic_string(name),reinterpret_cast<std::uintptr_t>(replay_control_window),center.x,center.y,m_replay_tick.load());
            };

            const auto index_progress=m_replay_progress_phase.load();
            const bool indexing=index_progress==1 || index_progress==2;
            if(index_progress) {
                ImGui::SetNextWindowPos(ImVec2(m_replay_drawing?300.0f:24.0f,24.0f),ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(360,170),ImGuiCond_Always);
                ImGui::Begin("Replay indexing",nullptr,ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
                ImGui::Text("Indexed through tick %llu",static_cast<unsigned long long>(m_replay_index_observed.load()));
                if(index_progress==1)ImGui::TextWrapped("Seeking becomes available after the complete indexing pass.");
                else if(index_progress==2)ImGui::TextWrapped("Cancelling indexing. Waiting for owned resources to retire.");
                else ImGui::TextWrapped("Indexing failed (code %u). Seeking is unavailable.",m_replay_progress_failure.load());
                ImGui::BeginDisabled(!m_replay_index_can_cancel.load());
                if(ImGui::Button("Cancel indexing",ImVec2(210,32))) {
                    m_replay_index_cancel.store(true);
                    RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] replay index cancellation requested\n"));
                }
                ImGui::EndDisabled();
                ImGui::End();
            }

            // Dispatch registered callbacks.  Snapshot the pointer
            // under acquire semantics so register/unregister racing
            // with us doesn't observe a torn vector.
            if(!m_replay_drawing && m_replay_index_complete.load()) {
                ImGui::SetNextWindowPos(ImVec2(24,24),ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(290,135),ImGuiCond_Always);
                ImGui::Begin("Replay playback",nullptr,ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
                ImGui::Text("Tick %llu",static_cast<unsigned long long>(m_replay_tick.load()));
                ImGui::BeginDisabled(!m_replay_can_pause.load());
                if(ImGui::Button("Pause replay",ImVec2(210,32)))m_replay_pause_request.store(true);
                if(m_replay_can_pause.load() && !m_replay_pause_control_reported){report_replay_button("pause");m_replay_pause_control_reported=true;focus_replay_playback=true;}
                ImGui::EndDisabled();
                if(m_replay_pause_rejected.load())ImGui::TextWrapped("Pause unavailable at this boundary. Try again when playback is ready.");
                ImGui::End();
            }
            if(!m_replay_can_pause.load())m_replay_pause_control_reported=false;
            if(!m_replay_drawing){m_replay_numeric_open_reported=false;m_replay_exit_reported=false;}
            if (m_replay_drawing)
            {
                ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_Always);
                const bool indexed=m_replay_index_complete.load();
                ImGui::SetNextWindowSize(ImVec2(indexed?420.0f:260.0f, indexed?440.0f:218.0f), ImGuiCond_Always);
                ImGui::Begin("Replay paused", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
                ImGui::Text("Paused at tick %llu", static_cast<unsigned long long>(m_replay_tick.load()));
                const bool retained_end=indexed && m_replay_tick.load()>=m_replay_last_tick.load();
                ImGui::BeginDisabled(retained_end || indexing);
                if (ImGui::Button("Resume", ImVec2(210, 32)))
                {
                    const auto request = m_replay_resume_requests.fetch_add(1) + 1;
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[GameImGui] replay resume activated request={} surface_frame={}\n"), request, m_replay_frames.load());
                }
                ImGui::SetItemDefaultFocus();
                if (!m_replay_controls_reported)
                {
                    const auto low = ImGui::GetItemRectMin();
                    const auto high = ImGui::GetItemRectMax();
                    POINT center{static_cast<LONG>((low.x + high.x) * 0.5f), static_cast<LONG>((low.y + high.y) * 0.5f)};
                    if (ClientToScreen(replay_control_window, &center))
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[GameImGui] replay resume control hwnd={} screen_x={} screen_y={}\n"),
                            reinterpret_cast<std::uintptr_t>(replay_control_window), center.x, center.y);
                        m_replay_controls_reported = true;
                    }
                }
                ImGui::EndDisabled();
                if(retained_end) ImGui::TextWrapped("Replay complete. Seek backward to continue.");
                ImGui::BeginDisabled(!m_replay_can_step.load() || indexing);
                if (ImGui::Button("Step one tick", ImVec2(210, 32)))
                {
                    const auto request = m_replay_step_requests.fetch_add(1) + 1;
                    RC::Output::send<RC::LogLevel::Default>(STR(
                        "[GameImGui] replay step activated request={} surface_frame={}\n"), request, m_replay_frames.load());
                }
                if (!m_replay_step_reported && m_replay_can_step.load())
                {
                    const auto low = ImGui::GetItemRectMin(), high = ImGui::GetItemRectMax();
                    POINT center{static_cast<LONG>((low.x+high.x)*0.5f),static_cast<LONG>((low.y+high.y)*0.5f)};
                    if (ClientToScreen(replay_control_window, &center))
                    {
                        RC::Output::send<RC::LogLevel::Default>(STR(
                            "[GameImGui] replay step control hwnd={} screen_x={} screen_y={}\n"),
                            reinterpret_cast<std::uintptr_t>(replay_control_window),center.x,center.y);
                        m_replay_step_reported = true;
                    }
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!m_replay_can_cancel.load());
                if(ImGui::Button("Cancel seek",ImVec2(210,32))) {
                    const auto request=m_replay_cancel_requests.fetch_add(1)+1;
                    RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] replay cancel activated request={} surface_frame={}\n"),request,m_replay_frames.load());
                }
                if(m_replay_can_cancel.load() && m_replay_seek_failed.load()) {
                    const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
                    POINT center{static_cast<LONG>((low.x+high.x)*0.5f),static_cast<LONG>((low.y+high.y)*0.5f)};
                    if(!m_replay_cancel_reported && ClientToScreen(replay_control_window,&center)) {
                        RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] replay cancel control hwnd={} screen_x={} screen_y={}\n"),reinterpret_cast<std::uintptr_t>(replay_control_window),center.x,center.y);
                        m_replay_cancel_reported=true;
                    }
                } else m_replay_cancel_reported=false;
                ImGui::EndDisabled();
                if(m_replay_seek_failed.load()) ImGui::TextWrapped("Seek failed. Cancel requests recovery; Resume can dismiss a clean failed request after pending work finishes.");
                if(indexed) {
                    const std::uint64_t first=0,last=m_replay_last_tick.load();
                    ImGui::Separator();
                    ImGui::Text("Indexed ticks: 0 - %llu",static_cast<unsigned long long>(last));
                    if(m_replay_unsupported_tail.load())ImGui::TextWrapped("Retained replay range. Later victory/finish ticks are unsupported; native reload is required to play that tail.");
                    if(const auto round=m_replay_round.load();round>=0)
                        ImGui::Text("Round %d, round tick %u",round+1,m_replay_round_tick.load());
                    const auto checkpoint=m_replay_first_checkpoint.load();
                    const bool has_checkpoint=checkpoint!=UINT64_MAX && checkpoint<=last;
                    if(!has_checkpoint) ImGui::TextWrapped("No checkpoint retained. Seeking is unavailable.");
                    else if(checkpoint) ImGui::TextWrapped("Ticks 0 - %llu are indexed but currently unsupported: checkpoint history starts at %llu.",
                        static_cast<unsigned long long>(checkpoint-1),static_cast<unsigned long long>(checkpoint));
                    // The full index remains visible. A retained checkpoint is
                    // not proof of live bindings; the host still validates each
                    // seek and may select an earlier restorable checkpoint.
                    ImGui::BeginDisabled(!has_checkpoint);
                    if(!ImGui::IsAnyItemActive()) m_replay_tick_entry=m_replay_tick.load();
                    if(ImGui::SliderScalar("Timeline",ImGuiDataType_U64,&m_replay_tick_entry,&first,&last,"%llu"))
                        m_replay_seek_request.store(m_replay_tick_entry);
                    ImGui::Text("Target tick: %llu",static_cast<unsigned long long>(m_replay_tick_entry));
                    if(ImGui::Button("Enter tick")) {
                        m_replay_numeric_entry.reset(m_replay_tick.load());
                        m_replay_numeric_frames=0;
                        ImGui::OpenPopup("Replay tick number");
                    }
                    if(!m_replay_numeric_open_reported){report_replay_button("number_open");m_replay_numeric_open_reported=true;}
                    // Use the already owned held-view mouse transitions. This
                    // popup never pumps or synthesizes native keyboard input.
                    if(ImGui::BeginPopup("Replay tick number")) {
                        if(m_replay_numeric_frames<3)++m_replay_numeric_frames;
                        const bool report_numbers=m_replay_numeric_frames==2;
                        if(m_replay_numeric_entry.empty())ImGui::TextUnformatted("Tick: (empty)");
                        else ImGui::Text("Tick: %llu",static_cast<unsigned long long>(m_replay_numeric_entry.value()));
                        for(unsigned digit=1;digit<=9;++digit) {
                            const char label[]{static_cast<char>('0'+digit),0};
                            if(ImGui::Button(label,ImVec2(54,32)))m_replay_numeric_entry.append(digit);
                            if(report_numbers)report_replay_button(label);
                            if(digit%3)ImGui::SameLine();
                        }
                        if(ImGui::Button("0",ImVec2(54,32)))m_replay_numeric_entry.append(0);
                        if(report_numbers)report_replay_button("0");
                        ImGui::SameLine();
                        if(ImGui::Button("Erase",ImVec2(54,32)))m_replay_numeric_entry.erase();
                        ImGui::SameLine();
                        if(ImGui::Button("Clear",ImVec2(54,32)))m_replay_numeric_entry.clear();
                        if(report_numbers)report_replay_button("number_clear");
                        if(!m_replay_numeric_entry.empty() && !m_replay_numeric_entry.valid(last))
                            ImGui::Text("Maximum tick: %llu",static_cast<unsigned long long>(last));
                        ImGui::BeginDisabled(!m_replay_numeric_entry.valid(last));
                        if(ImGui::Button("Seek to tick",ImVec2(178,32))) {
                            m_replay_tick_entry=m_replay_numeric_entry.value();
                            m_replay_seek_request.store(m_replay_tick_entry);
                            RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] numeric replay seek submitted target={}\n"),m_replay_tick_entry);
                            ImGui::CloseCurrentPopup();
                        }
                        if(report_numbers)report_replay_button("number_submit");
                        ImGui::EndDisabled();
                        if(ImGui::Button("Dismiss",ImVec2(178,32)))ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    if(ImGui::Button("Previous round"))m_replay_seek_request.store(previous_round_request);
                    ImGui::SameLine();
                    if(ImGui::Button("Next round"))m_replay_seek_request.store(next_round_request);
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!m_replay_can_exit.load());
                    if(ImGui::Button("Exit replay"))m_replay_seek_request.store(exit_replay_request);
                    if(m_replay_can_exit.load() && !m_replay_exit_reported){report_replay_button("exit");m_replay_exit_reported=true;}
                    if(!m_replay_can_exit.load())m_replay_exit_reported=false;
                    ImGui::EndDisabled();
                    if(const auto failure=m_replay_index_failure.load())
                        ImGui::TextWrapped("Target unavailable (code %u). The current hold is retained.",failure);
                }
                ImGui::End();
            }
            else if (visible && !m_replay_armed)
            {
                auto cbs = m_callbacks.load();
                if (cbs)
                {
                    for (const auto& entry : *cbs)
                    {
                        // Each callback is a stand-alone widget; exceptions
                        // in one must not poison the others.
                        try { entry.cb(); } catch (...) { /* swallow */ }
                    }
                }
            }

            // The ordinary HorseMod window may focus itself on the same show
            // edge. Bring the newly available playback panel above it once,
            // after callbacks; never steal focus repeatedly from user controls.
            if(focus_replay_playback)ImGui::SetWindowFocus("Replay playback");
            bool passive_keep_alive = false;
            if (passive_requested && !m_replay_drawing)
            {
                auto passive_cbs = m_passive_callbacks.load();
                if (passive_cbs)
                {
                    for (const auto& entry : *passive_cbs)
                    {
                        try
                        {
                            passive_keep_alive =
                                entry.cb() || passive_keep_alive;
                        }
                        catch (...)
                        {
                            // Passive drawing is best-effort only.
                        }
                    }
                }
            }

            ImGui::Render();
            io.ConfigFlags = saved_config_flags;
            if (passive_requested && !m_replay_drawing)
            {
                if (passive_keep_alive)
                {
                    m_passive_draw_requested.store(true,
                                                   std::memory_order_release);
                }
                else if (m_passive_draw_generation.load(
                             std::memory_order_acquire)
                         == passive_generation)
                {
                    m_passive_draw_requested.store(
                        false, std::memory_order_release);
                }
            }

            // Bind the game's back buffer and draw our vertex data
            // onto it.  We intentionally leave the preceding render-
            // target state alone — the game's renderer sets up its
            // own RTV on the NEXT frame, and Steam's overlay hook
            // runs AFTER this, compositing its UI on top of whatever
            // we drew.
            ID3D11RenderTargetView* rtv = m_replay_drawing ? m_replay_output_rtv.Get() : state.back_buffer_rtv();
            if (rtv)
            {
                state.context()->OMSetRenderTargets(1, &rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
        }

        // Optional diagnostic observer sees this output's actual backbuffer
        // after UI drawing, before discard/Present. It must own any queued copy
        // and fence callback retirement; it never supplies simulation state.
        if(m_replay_drawing) if(const auto observer=m_replay_output_observer.load())
            observer->callback(observer->owner,swap_chain,m_replay_tick.load());
        // Chain to the real Present (which, if Steam is loaded, is
        // Steam's Present hook; it then chains to the real DXGI
        // function).  Return the HRESULT verbatim so the game sees
        // the same Present semantics it would without us.
        return present_replay_frame(swap_chain,sync_interval,flags);
    }

    // -------- Probe swap chain (kept alive for hook lifetime) --------

    inline bool PresentHook::create_probe_swap_chain()
    {
        // Create a message-only window (no visual footprint, not on
        // the taskbar, no WM_PAINT, no focus interaction).
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = m_probe_class_name;
        RegisterClassExW(&wc);
        m_probe_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0,
                                        0, 0, 1, 1,
                                        HWND_MESSAGE, nullptr,
                                        wc.hInstance, nullptr);
        if (!m_probe_hwnd)
        {
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferCount        = 1;
        desc.BufferDesc.Width   = 1;
        desc.BufferDesc.Height  = 1;
        desc.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow       = m_probe_hwnd;
        desc.SampleDesc.Count   = 1;
        desc.Windowed           = TRUE;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL got_level{};

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, levels, _countof(levels),
            D3D11_SDK_VERSION, &desc,
            m_probe_swap_chain.GetAddressOf(),
            m_probe_device.GetAddressOf(),
            &got_level,
            m_probe_context.GetAddressOf());
        if (FAILED(hr) || !m_probe_swap_chain)
        {
            DestroyWindow(m_probe_hwnd);
            m_probe_hwnd = nullptr;
            UnregisterClassW(m_probe_class_name, wc.hInstance);
            RC::Output::send<RC::LogLevel::Error>(
                STR("[GameImGui] D3D11CreateDeviceAndSwapChain failed hr=0x{:08X}\n"),
                static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    }

    inline bool PresentHook::arm_replay_surface(std::uint64_t budget_bytes)
    {
        if (GetCurrentThreadId() != m_present_thread.load() || !m_game_swap_chain
            || !m_installed.load() || m_replay_armed || !budget_bytes) return false;
        const bool transferred=m_replay_transfer;
        if(transferred) {
            if(!m_replay_ready.load() || !m_replay_image || m_replay_native_pending
                || replay_surface_bytes()>budget_bytes) return false;
            ComPtr<ID3D11Texture2D> current;
            if(FAILED(m_game_swap_chain->GetBuffer(0,IID_PPV_ARGS(current.GetAddressOf())))) return false;
            D3D11_TEXTURE2D_DESC saved{},live{};
            m_replay_image->GetDesc(&saved);current->GetDesc(&live);
            ComPtr<ID3D11Device> saved_device,current_device;
            m_replay_image->GetDevice(saved_device.GetAddressOf());current->GetDevice(current_device.GetAddressOf());
            if(saved_device.Get()!=current_device.Get() || saved.Width!=live.Width || saved.Height!=live.Height
                || saved.Format!=live.Format || saved.SampleDesc.Count!=live.SampleDesc.Count) return false;
            // The previous image may be shared with an immutable checkpoint.
            // Detach before native Present can capture into the mutable display.
            const auto bytes=m_replay_bytes.load();
            const auto owned=replay_surface_bytes();
            if(!bytes || owned>budget_bytes || bytes+256>budget_bytes-owned) {
                RC::Output::send<RC::LogLevel::Warning>(STR("[GameImGui] replay surface arm rejected check=transfer_capacity owned={} envelope={} budget={} ready={} native_pending={}\n"),
                    owned,bytes+256,budget_bytes,m_replay_ready.load(),m_replay_native_pending.load());
                return false;
            }
            auto& dx=DX11State::instance();
            if(!dx.ready() || saved_device.Get()!=dx.device()) return false;
            ComPtr<ID3D11Texture2D> replacement;
            ComPtr<ID3D11Query> completion;
            const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            if(FAILED(dx.device()->CreateTexture2D(&saved,nullptr,replacement.GetAddressOf()))
                || FAILED(dx.device()->CreateQuery(&query,completion.GetAddressOf()))) return false;
            m_replay_ready.store(false);
            m_replay_native_target=m_replay_image; // In-flight source lease, never overwritten.
            m_replay_native_context=dx.context();
            m_replay_native_completion=std::move(completion);
            m_replay_image=std::move(replacement);
            m_replay_transfer_bytes.store(bytes+256);
            m_replay_native_pending=true;
            dx.context()->CopyResource(m_replay_image.Get(),m_replay_native_target.Get());
            dx.context()->End(m_replay_native_completion.Get());
            dx.context()->Flush();
            const auto deadline=GetTickCount64()+500;
            while(!retire_replay_surface()) {
                if(GetTickCount64()>=deadline) {
                    RC::Output::send<RC::LogLevel::Error>(STR("[GameImGui] replay display transfer completion failed retained=true bytes={}\n"),replay_surface_bytes());
                    return false;
                }
                Sleep(0);
            }
        }
        if (!prepare_replay_output(budget_bytes)) return false;
        m_replay_budget = budget_bytes;
        m_replay_frames.store(0);
        m_replay_ready.store(transferred);
        m_replay_capture_reported = false;
        m_replay_controls_reported = false;
        m_replay_resume_requests.store(0);
        m_replay_step_requests.store(0);
        m_replay_cancel_requests.store(0);
        m_replay_can_cancel.store(false);
        m_replay_seek_failed.store(false);
        m_replay_cancel_reported=false;
        m_replay_can_step.store(false);
        m_replay_step_reported = false;
        RC::Output::send<RC::LogLevel::Default>(STR(
            "[GameImGui] replay surface armed render_thread={} budget={}\n"),
            GetCurrentThreadId(), budget_bytes);
        m_replay_armed = true;
        m_replay_transfer = false;
        if(transferred) RC::Output::send<RC::LogLevel::Default>(STR("[GameImGui] replay display transferred to tick advance bytes={} previous_present={} native_target_rendered=false\n"),replay_surface_bytes(),m_replay_source_present);
        return true;
    }

    inline void PresentHook::capture_replay_surface(IDXGISwapChain* swap_chain)
    {
        auto& state = DX11State::instance();
        ComPtr<ID3D11Texture2D> source;
        if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(source.GetAddressOf()))))
        { m_replay_ready.store(false); return; }
        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        if (!m_replay_image && !m_replay_capture_reported)
        {
            m_replay_capture_reported = true;
            RC::Output::send<RC::LogLevel::Default>(STR(
                "[GameImGui] replay surface source width={} height={} format={} samples={} mips={} array={}\n"),
                description.Width, description.Height, static_cast<unsigned>(description.Format),
                description.SampleDesc.Count, description.MipLevels, description.ArraySize);
        }
        if ((description.Format != DXGI_FORMAT_R8G8B8A8_UNORM
                && description.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                && description.Format != DXGI_FORMAT_B8G8R8A8_UNORM
                && description.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
                && description.Format != DXGI_FORMAT_R10G10B10A2_UNORM)
            || description.SampleDesc.Count != 1 || description.MipLevels != 1 || description.ArraySize != 1
            || !description.Width || !description.Height
            || description.Width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
            || description.Height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        { m_replay_ready.store(false); return; }
        // The observed SC6 back buffer uses R10G10B10A2_UNORM even with
        // display HDR disabled. All admitted formats occupy four bytes per
        // texel; preserve the exact format for CopyResource and presentation.
        const auto bytes = std::uint64_t{description.Width} * description.Height * 4;
        const auto other_bytes=m_replay_scratch_bytes.load()+m_replay_transfer_bytes.load();
        if(other_bytes>m_replay_budget || bytes>m_replay_budget-other_bytes) { m_replay_ready.store(false); return; }
        if (m_replay_image)
        {
            D3D11_TEXTURE2D_DESC previous{};
            m_replay_image->GetDesc(&previous);
            if (previous.Width != description.Width || previous.Height != description.Height
                || previous.Format != description.Format)
            {
                m_replay_ready.store(false);
                m_replay_image.Reset();
                m_replay_bytes.store(0);
            }
        }
        if (!m_replay_image)
        {
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = description.CPUAccessFlags = description.MiscFlags = 0;
            if (FAILED(state.device()->CreateTexture2D(&description, nullptr, m_replay_image.GetAddressOf())))
            { m_replay_ready.store(false); return; }
            m_replay_bytes.store(bytes);
        }
        // Capture before ImGui so held redraws cannot accumulate the overlay.
        state.context()->CopyResource(m_replay_image.Get(), source.Get());
        m_replay_source_present = m_present_count.load(std::memory_order_relaxed);
        m_replay_ready.store(true);
    }

#include "ReplayNativeViewport.inl"

    inline bool PresentHook::draw_replay_surface()
    {
        auto* draw_image=replay_draw_image();
        if(GetCurrentThreadId()!=m_present_thread.load() || !m_replay_armed || !m_replay_ready.load()
            || !draw_image || !m_replay_output_chain || m_replay_drawing
            || m_replay_native_pending) return false;
        auto& state=DX11State::instance();
        D3D11_TEXTURE2D_DESC source{},output{};draw_image->GetDesc(&source);m_replay_output_image->GetDesc(&output);
        if(source.Width!=output.Width || source.Height!=output.Height || source.Format!=output.Format
            || !IsWindow(m_replay_output_window) || m_replay_output_window!=state.hwnd()) return false;
        RECT rectangle{};POINT origin{};
        if(!GetClientRect(state.hwnd(),&rectangle) || !ClientToScreen(state.hwnd(),&origin)
            || rectangle.right!=static_cast<LONG>(source.Width) || rectangle.bottom!=static_cast<LONG>(source.Height)) return false;
        OffsetRect(&rectangle,origin.x,origin.y);
        // Keep the original native render targets and stages untouched across
        // this out-of-band UI draw. The backend already restores VS/PS/GS/IA/RS.
        ID3D11RenderTargetView* targets[8]{};ID3D11DepthStencilView* depth{};
        ID3D11UnorderedAccessView* unordered[8]{};UINT counts[8];for(auto& count:counts) count=UINT(-1);
        state.context()->OMGetRenderTargets(8,targets,&depth);
        UINT target_count=0;for(UINT i=0;i<8;++i) if(targets[i]) target_count=i+1;
        if(target_count<8) state.context()->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,
            target_count,8-target_count,unordered+target_count);
        ComPtr<ID3D11Predicate> predicate;BOOL predicate_value{};
        state.context()->GetPredication(predicate.GetAddressOf(),&predicate_value);
        ID3D11HullShader* hs{};ID3D11DomainShader* ds{};ID3D11ComputeShader* cs{};
        ID3D11ClassInstance* hc[256]{},*dc[256]{},*cc[256]{};UINT hn=256,dn=256,cn=256;
        state.context()->HSGetShader(&hs,hc,&hn);state.context()->DSGetShader(&ds,dc,&dn);state.context()->CSGetShader(&cs,cc,&cn);
        m_replay_native_target=draw_image;m_replay_native_context=state.context();m_replay_native_pending=true;
        state.context()->SetPredication(nullptr,FALSE);
        const bool staged=stage_replay_native_draw();
        state.bind_imgui_context();const auto before=ImGui::GetFrameCount();
        if(!m_replay_mouse_active.load()) {
            m_replay_mouse_begin.store(GetTickCount());m_replay_mouse_seen.store(true);m_replay_mouse_active.store(true);
        }
        m_replay_drawing=true;
        const auto result=staged?on_present(m_replay_output_chain.Get(),0,0):E_FAIL;
        m_replay_drawing=false;
        const bool restored=!staged || restore_replay_native_backbuffer();
        state.context()->OMSetRenderTargetsAndUnorderedAccessViews(target_count,targets,depth,
            target_count,8-target_count,unordered+target_count,counts+target_count);
        state.context()->SetPredication(predicate.Get(),predicate_value);
        state.context()->HSSetShader(hs,hc,hn);state.context()->DSSetShader(ds,dc,dn);state.context()->CSSetShader(cs,cc,cn);
        for(auto* target:targets) if(target) target->Release();if(depth) depth->Release();
        for(auto* view:unordered) if(view) view->Release();
        if(hs) hs->Release();if(ds) ds->Release();if(cs) cs->Release();
        for(UINT i=0;i<hn;++i) hc[i]->Release();for(UINT i=0;i<dn;++i) dc[i]->Release();for(UINT i=0;i<cn;++i) cc[i]->Release();
        state.context()->End(m_replay_native_completion.Get());state.context()->Flush();
        const auto deadline=GetTickCount64()+500;
        while(!retire_replay_surface()) {
            if(GetTickCount64()>=deadline) {
                RC::Output::send<RC::LogLevel::Error>(STR("[GameImGui] native replay viewport completion failed retained=true\n"));
                return false;
            }
            SwitchToThread();
        }
        if(result!=S_OK || !restored || (g_overlay_visible.load() && ImGui::GetFrameCount()==before)) return false;
        if(!m_replay_frames.load()) RC::Output::send<RC::LogLevel::Default>(STR(
            "[GameImGui] native replay viewport displayed tick={} hwnd={} backbuffer_restored=true gpu_complete=true\n"),
            m_replay_tick.load(),reinterpret_cast<std::uintptr_t>(m_replay_output_window));
        m_replay_frames.fetch_add(1);return true;
    }

    inline bool PresentHook::retire_replay_surface()
    {
        if (GetCurrentThreadId() != m_present_thread.load() || m_replay_drawing) return false;
        if (!m_replay_native_pending) return true;
        if (!m_replay_native_context || !m_replay_native_completion) return false;
        BOOL done = FALSE;
        const auto result = m_replay_native_context->GetData(m_replay_native_completion.Get(), &done, sizeof(done), 0);
        if (result != S_OK || !done) return false;
        m_replay_native_pending = false;
        m_replay_native_target.Reset();
        m_replay_transfer_bytes.store(0);
        m_replay_native_context.Reset();
        return true;
    }

    inline bool PresentHook::release_replay_surface(bool transfer_to_advance)
    {
        if (GetCurrentThreadId() != m_present_thread.load() || m_replay_drawing || m_replay_native_pending
            || (m_replay_display_transaction && !transfer_to_advance)) return false;
        if(transfer_to_advance && (!m_replay_armed || !m_replay_ready.load() || !m_replay_image)) return false;
        m_replay_transfer=transfer_to_advance;
        m_replay_mouse_end.store(GetTickCount());
        m_replay_mouse_active.store(false);
        m_replay_ready.store(transfer_to_advance);
        m_replay_armed = false;
        if(transfer_to_advance) return true; // Retain completed GPU/display ownership and its accounting.
        if(!release_replay_output()) return false;
        m_replay_native_completion.Reset();
        m_replay_scratch_bytes.store(0);
        m_replay_image.Reset();
        m_replay_bytes.store(0);
        return true;
    }

#include "ReplaySurfaceTransaction.inl"

    inline bool PresentHook::consumed_replay_mouse_message(DWORD message_time) const noexcept
    {
        if (!m_replay_mouse_seen.load()) return false;
        const auto begin = m_replay_mouse_begin.load();
        const auto end = m_replay_mouse_active.load() ? GetTickCount() : m_replay_mouse_end.load();
        // Unsigned subtraction handles the Win32 millisecond counter wrapping.
        // This bounded replay lease never admits a 24-day held interval.
        const DWORD duration = end - begin;
        return duration < 0x80000000u && message_time - begin <= duration;
    }

    inline void PresentHook::destroy_probe_swap_chain()
    {
        m_probe_swap_chain.Reset();
        m_probe_context.Reset();
        m_probe_device.Reset();
        if (m_probe_hwnd)
        {
            DestroyWindow(m_probe_hwnd);
            UnregisterClassW(m_probe_class_name, GetModuleHandleW(nullptr));
            m_probe_hwnd = nullptr;
        }
    }
}
