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
// whatever thread the game uses for presentation (SC6: game thread).
// ============================================================================

#pragma once

#include "DX11State.hpp"
#include "GamepadInput.hpp"

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

    class PresentHook
    {
    public:
        static PresentHook& instance()
        {
            static PresentHook s;
            return s;
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
        mutable std::atomic<std::shared_ptr<const std::vector<CallbackEntry>>> m_callbacks;
        std::atomic<uint64_t> m_next_token{1};

        // Rising-edge callback for gamepad BACK button.  Written by
        // set_on_gamepad_back (from init thread), read by the render
        // thread via GamepadInput::feed_to_imgui's edge detector.
        // std::function is not atomic, but we only ever publish it
        // ONCE at init and never change it thereafter — the render
        // thread's read happens after init completes, so no races.
        std::function<void()> m_on_gamepad_back;

        std::atomic<uint64_t> m_present_count{0};
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

        // Publish an empty callback vector so imgui-wants-input reads
        // are safe from first-frame races.
        m_callbacks.store(std::make_shared<const std::vector<CallbackEntry>>());

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

        PLH::VFuncMap originals;  // PolyHook writes the old function
                                   // pointers here after hook.
        m_vfunc_hook = std::make_unique<PLH::VFuncSwapHook>(
            reinterpret_cast<uint64_t>(m_probe_swap_chain.Get()),
            redirects,
            &originals);
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
        if (auto it = originals.find(kIDXGISwapChain_Present); it != originals.end() && it->second)
        {
            m_original_present = reinterpret_cast<Present_t>(it->second);
        }
        if (auto it = originals.find(kIDXGISwapChain_ResizeBuffers); it != originals.end() && it->second)
        {
            m_original_resize_buffers = reinterpret_cast<ResizeBuffers_t>(it->second);
        }

        m_installed.store(true, std::memory_order_release);
        RC::Output::send<RC::LogLevel::Default>(
            STR("[GameImGui] PresentHook installed (orig Present={} orig Resize={})\n"),
            reinterpret_cast<uintptr_t>(m_original_present),
            reinterpret_cast<uintptr_t>(m_original_resize_buffers));
        return true;
    }

    // SEH-wrapped unHook — must be a free function (no C++ unwinding
    // frames in scope) because __try/__except can't sit alongside
    // C++ destructors.  Returns true if unHook ran cleanly, false if
    // it faulted (which we eat — we're shutting down anyway).
    //
    // Why this is wrapped at all:
    //   PLH::VFuncSwapHook::unHook iterates an std::map and writes
    //   restored function pointers back into a DXGI swap-chain
    //   vtable.  During UNCLEAN shutdown (e.g. the game crashed and
    //   DXGI was already torn down by its crash handler), that
    //   vtable's memory has been decommitted by the OS.  Without
    //   this guard, writing to it raises a second-chance AV that
    //   replaces the ORIGINAL crash in the minidump — making the
    //   real bug invisible.  Empirical: a 2026-04-29 crash ate the
    //   original cause and surfaced this path's `MOV RAX, [RAX]`
    //   on a freed std::map head pointer (val=0x1) at HorseMod
    //   image-offset 0x987d6.
    static bool try_unhook_seh(PLH::VFuncSwapHook* h) noexcept
    {
        __try
        {
            h->unHook();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    inline void PresentHook::uninstall()
    {
        if (!m_installed.exchange(false)) return;
        if (m_vfunc_hook)
        {
            // unHook() can fault during process-shutdown teardown if
            // DXGI / D3D11 / their heap pages were already released
            // by the OS.  Catch the AV so we don't replace the
            // original crash with our cleanup's secondary fault.
            // The OS is going to reclaim everything in a moment
            // anyway — leaving the vtable un-restored is harmless.
            const bool clean = try_unhook_seh(m_vfunc_hook.get());
            if (!clean)
            {
                RC::Output::send<RC::LogLevel::Warning>(
                    STR("[GameImGui] VFuncSwapHook::unHook faulted "
                        "during teardown (DXGI already torn down?) "
                        "— swallowed; OS will reclaim the vtable.\n"));
            }
            m_vfunc_hook.reset();
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

    // -----------------------------------------------------------------
    // Context-safe IO query.  WndProc runs on the UI thread while the
    // render thread is inside Present (possibly flipping the current
    // ImGui context).  ImGui::GetIO() reads the *current* context, so
    // we save/restore around the read to guarantee we query ours.
    // -----------------------------------------------------------------
    namespace detail
    {
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
        auto& self = instance();
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

        auto& state = DX11State::instance();

        // Lazy init on first hooked Present — we NOW have the real
        // swap chain and thus the game's device/context/HWND.
        bool init_ok = state.ensure_initialised(swap_chain);

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
        GamepadInput::instance().poll_and_detect_back(m_on_gamepad_back);

        const bool visible =
            g_overlay_visible.load(std::memory_order_relaxed);

        if (visible && init_ok && state.imgui_context())
        {
            // Bind OUR context before any ImGui:: call.  UE4SS might
            // have set its own context via UE4SS_ENABLE_IMGUI(), and
            // tab callbacks will run ImGui:: functions.  They must
            // target our context, not UE4SS's.
            state.bind_imgui_context();

            // Feed the cached gamepad state into ImGui for nav.  We
            // only call this when the frame is actually going to
            // NewFrame / Render — otherwise the queued AddKeyEvent
            // calls would pile up in a context that never processes
            // them (leak).
            GamepadInput::instance().feed_nav_to_imgui(state.imgui_context());

            // --- ImGui frame ---
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Dispatch registered callbacks.  Snapshot the pointer
            // under acquire semantics so register/unregister racing
            // with us doesn't observe a torn vector.
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

            ImGui::Render();

            // Bind the game's back buffer and draw our vertex data
            // onto it.  We intentionally leave the preceding render-
            // target state alone — the game's renderer sets up its
            // own RTV on the NEXT frame, and Steam's overlay hook
            // runs AFTER this, compositing its UI on top of whatever
            // we drew.
            ID3D11RenderTargetView* rtv = state.back_buffer_rtv();
            if (rtv)
            {
                state.context()->OMSetRenderTargets(1, &rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }
        }

        // Chain to the real Present (which, if Steam is loaded, is
        // Steam's Present hook; it then chains to the real DXGI
        // function).  Return the HRESULT verbatim so the game sees
        // the same Present semantics it would without us.
        return m_original_present(swap_chain, sync_interval, flags);
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
