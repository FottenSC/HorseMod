// ============================================================================
// Horse::FreeCamera — WASD + look-keys fly camera, independent of Nvidia Ansel.
//
// Motivation
// ----------
// Users have asked for "Ansel-style camera movement without Nvidia Ansel"
// because invoking Ansel's photo-mode overlay disables HorseMod's own
// ULineBatchComponent-based hitbox overlay (Ansel retopologises the render
// pipeline and our lines don't survive).  This module provides an equivalent
// free-fly camera by writing directly to the engine's CameraCache POV while
// CamLock holds off the engine's own stores.
//
// Mechanism — and the object identity it hinges on
// ------------------------------------------------
// The object the renderer actually reads from is the APlayerCameraManager,
// NOT the ALuxBattleCamera actor.  This distinction caused a false start:
//
//   * LuxBattleManager.BattleCamera is an ALuxBattleCamera (ACameraActor
//     subclass).  That's a "director" camera — an input to the manager,
//     not the render source itself.
//   * APlayerCameraManager has an embedded FCameraCacheEntry at +0x400,
//     whose FMinimalViewInfo sits at +0x410..+0x44B:
//
//         +0x400  float  TimeStamp          (gate — > 0.0f during active battle)
//         +0x410  float  Location.X
//         +0x414  float  Location.Y
//         +0x418  float  Location.Z
//         +0x41C  float  Rotation.Pitch
//         +0x420  float  Rotation.Yaw
//         +0x424  float  Rotation.Roll
//         +0x428  float  FOV
//         +0x42C  float  DesiredFOV
//         (...)   AspectRatio / OrthoWidth / PostProcess tail
//
//   * Ghidra trace (UWorld::Tick at 0x141f02230, inside the AController
//     iteration loop) shows the engine calls the per-tick commit path
//     as `APlayerCameraManager_CommitPOV_NoInterp(plVar15[0x84])` —
//     i.e. this = PlayerController->PlayerCameraManager.  The 5 NOP
//     targets in Horse::CamLock all write to `this+0x410..+0x428` on
//     this same PCM instance.
//   * APlayerController::GetPlayerViewPoint @ 0x142046410 and
//     ::GetCameraViewLoc @ 0x142042730 read back from the same
//     +0x410..+0x424 block on PCM, and those are what the renderer
//     (via ULocalPlayer::CalcSceneView) consumes every frame.
//
// So the contract is:
//   1. Enable CamLock (NOPs every engine-side store to PCM+0x410..+0x428).
//   2. Write our own values into PCM+0x410..+0x428 each cockpit tick.
// ... and the renderer obeys us.  CamLock is the "stop the engine fighting
// us" half; this class is the "drive the pose from user input" half.
//
// An earlier revision of this file incorrectly threaded the ALuxBattleCamera
// actor pointer from LuxBattleManager.BattleCamera as the write target.
// Memory-persistence diagnostics confirmed our writes WERE landing — just on
// the wrong object: the actor's +0x410..+0x428 is an unrelated post-process
// / director scratch block that nothing downstream reads.  The renderer
// happily kept consuming the frozen-by-CamLock PCM cache and the camera
// appeared locked.  If the user toggles F7 and sees the on-screen HUD
// pose-readout changing but no visual movement, that's the footprint of
// writing to the wrong object — check that `pcm` here is non-null.
//
// Input
// -----
// Keyboard (GetAsyncKeyState):
//   WASD          translate (W/S along camera-forward, A/D along camera-right)
//   Q / E         translate vertical (world-Z down / up)
//   Arrows or IJKL look (pitch / yaw)
//   Shift         5× speed multiplier
//   Ctrl          0.2× speed multiplier
//
// Note on arrow keys: SC6 appears to register the arrow cluster with
// RawInput's RIDEV_NOLEGACY flag (typical for fighters, reduces menu-
// input latency), which can suppress GetAsyncKeyState at the OS level.
// If arrows feel unresponsive, fall back to IJKL — those are regular
// letter keys that always register.
//
// Controller (XInput, player-0 gamepad only — plug one in and it's live):
//   Left stick    translate (forward / strafe)
//   Right stick   look (pitch / yaw)
//   LT / RT       vertical (down / up)
//   LB            0.2× speed
//   RB            5× speed
//   A button      (reserved — not bound; suggestion: use as a mod-toggle
//                  from the game pad without needing F7)
//
// Both input paths are additive: a keyboard key and controller stick
// contribution sum, so you can mix-and-match.
//
// We poll key state via GetAsyncKeyState each cockpit tick.  To avoid
// moving the camera when the user alt-tabs away, we skip polling if the
// game window is not the foreground window.  XInput reads the controller
// regardless of focus (XInput has no focus concept), so we also guard
// the controller contribution with the same focus check — otherwise the
// camera would drift when the user is using the pad to navigate a menu
// in another window.
//
// On OFF→ON transition we snapshot the current engine pose so there's no
// teleport.  On ON→OFF we release CamLock and the engine resumes director
// control.
//
// Caveats
// -------
//   * Roll isn't exposed to input — we just preserve whatever the engine
//     last wrote.  Most of the time that's zero; add a hotkey if needed.
//   * The camera will drift along with the battle stage's director
//     transforms if the engine ever re-commits a new base between our
//     writes.  We haven't observed this in practice with CamLock on, but
//     if it does we'd widen CamLock to cover any additional cache.
//   * Out-of-battle scenes (menus, replay intros) run a different camera
//     path and this feature is silent there — the toggle still flips but
//     has no visible effect until a match starts.
//
// Threading
// ---------
// set() and tick() are called from the cockpit hook (game thread).  All
// writes are single-float so torn reads by the renderer are visually
// negligible; we don't need atomics for the pose fields.
// ============================================================================

#pragma once

#include "CamLock.hpp"

#include <DynamicOutput/DynamicOutput.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <thread>
// XInput ships with multiple runtime redirects; Xinput.lib is the
// stable one (9.1.0 redist).  Linking via #pragma avoids having to
// edit CMakeLists for a single dependency.
#pragma comment(lib, "Xinput.lib")

namespace Horse
{
    // ------------------------------------------------------------------
    // LowLevelKeyInput — OS-level keyboard state maintained by a
    // WH_KEYBOARD_LL hook.  Why we need this:
    //
    // UE4SS's register_keydown_event and our own tick-time GetAsyncKeyState
    // calls both read the thread/system async key state.  For most keys
    // that's fine, but a subset of games (fighting games in particular)
    // register certain VKs through RawInput with RIDEV_NOLEGACY, which
    // suppresses both GetAsyncKeyState and the WM_KEYDOWN message queue
    // for those keys.  When this happens the arrow cluster stops
    // registering for us even though the user clearly pressed them.
    //
    // WH_KEYBOARD_LL runs below all of that: it's a global OS hook that
    // the keyboard driver calls BEFORE any application-level RawInput
    // consumer, so we see every press no matter what the host does.
    //
    // Lifetime: the singleton installs its hook lazily on first access
    // and keeps it alive for the life of the process.  Windows auto-
    // unhooks us if the hook callback takes too long (soft-timeout of
    // ~300ms by default), but our callback just sets a bool — nanoseconds.
    //
    // Safety: the hook fires on the OS keyboard thread's context.  Our
    // state array is an array of std::atomic<bool> so the main thread's
    // reads in tick() are race-free.  No allocations, no file I/O, no
    // blocking.
    // ------------------------------------------------------------------
    class LowLevelKeyInput
    {
    public:
        static LowLevelKeyInput& instance()
        {
            static LowLevelKeyInput s;
            return s;
        }

        // Is the given virtual-key currently pressed?  Only valid for
        // VKs in [0, 255].  Thread-safe; acquire-load only.
        bool is_down(int vk) const
        {
            if (vk < 0 || vk >= 256) return false;
            return m_down[vk].load(std::memory_order_acquire);
        }

        bool hook_installed() const { return m_hook != nullptr; }

    private:
        LowLevelKeyInput()
        {
            // Install the global low-level keyboard hook.  Must be called
            // from a thread that has (or will have) a message pump — the
            // injecting DLL's main thread is fine because UE4SS is driving
            // the game's message loop.  Use the current module handle so
            // the hook remains alive until we Unhook.
            m_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &hook_proc,
                                       GetModuleHandleW(nullptr), 0);
        }
        ~LowLevelKeyInput()
        {
            if (m_hook) UnhookWindowsHookEx(m_hook);
        }
        LowLevelKeyInput(const LowLevelKeyInput&) = delete;
        LowLevelKeyInput& operator=(const LowLevelKeyInput&) = delete;

        static LRESULT CALLBACK hook_proc(int nCode, WPARAM wParam, LPARAM lParam)
        {
            // Always chain before returning to avoid breaking downstream
            // hooks.  We process the event in parallel if it's actionable.
            if (nCode == HC_ACTION && lParam != 0)
            {
                const auto* info =
                    reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
                const DWORD vk = info->vkCode;
                if (vk < 256)
                {
                    auto& self = instance();
                    switch (wParam)
                    {
                        case WM_KEYDOWN:
                        case WM_SYSKEYDOWN:
                            self.m_down[vk].store(true,
                                std::memory_order_release);
                            break;
                        case WM_KEYUP:
                        case WM_SYSKEYUP:
                            self.m_down[vk].store(false,
                                std::memory_order_release);
                            break;
                        default:
                            break;
                    }
                }
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        HHOOK             m_hook = nullptr;
        std::atomic<bool> m_down[256]{};
    };

    // ------------------------------------------------------------------
    // RawInputSource — keyboard state from our own RawInput subscription.
    //
    // Why this exists after LowLevelKeyInput already exists: Steam Input
    // (and possibly SC6's own logic) can claim arrow keys in a way that
    // suppresses BOTH GetAsyncKeyState AND WH_KEYBOARD_LL — the exact
    // path is still open but the symptom is "LL hook says vk=0 while
    // the physical key is held".  RawInput, by contrast, lets multiple
    // subscribers each get their own independent copy of every HID event
    // from the device driver.  Registering our own subscription means
    // no other in-process consumer can hide events from us.
    //
    // Implementation:
    //   * A dedicated worker thread creates a message-only window
    //     (HWND_MESSAGE parent) and calls RegisterRawInputDevices with
    //     RIDEV_INPUTSINK (which routes events to our window even when
    //     it's not focused / not foreground).
    //   * The same thread runs a standard GetMessage / DispatchMessage
    //     pump so our WndProc receives WM_INPUT events.
    //   * Our WndProc decodes each RAWKEYBOARD and toggles an atomic
    //     bool in m_down[vk].
    //   * HorseMod's tick() polls m_down[] without any cross-thread
    //     synchronisation because atomic<bool> is race-free.
    //
    // Lifetime: singleton, thread started on first access, runs for
    // the life of the process.  The worker thread is detached — on
    // process exit the OS tears everything down cleanly.
    //
    // Safety: GetRawInputData is bounded to our local 128-byte buffer;
    // any oversize event is rejected.  Our WndProc delegates all other
    // messages to DefWindowProcW so we don't break the window's basic
    // contract with Windows.
    // ------------------------------------------------------------------
    class RawInputSource
    {
    public:
        static RawInputSource& instance()
        {
            static RawInputSource s;
            return s;
        }

        bool is_down(int vk) const
        {
            if (vk < 0 || vk >= 256) return false;
            return m_down[vk].load(std::memory_order_acquire);
        }

        bool ready() const { return m_ready.load(std::memory_order_acquire); }

    private:
        RawInputSource()
        {
            // Detach so the worker thread is torn down at process exit
            // via the OS rather than us joining at static-dtor time
            // (which would hang if the message pump is still alive).
            std::thread t([this] { this->worker_thread(); });
            t.detach();
        }
        RawInputSource(const RawInputSource&) = delete;
        RawInputSource& operator=(const RawInputSource&) = delete;

        void worker_thread()
        {
            // Register a private window class.  Using a unique name
            // keeps us from clashing with another mod that might try
            // the same trick.
            const wchar_t* kClass = L"HorseModRawInputWnd_vA";
            WNDCLASSEXW wc{};
            wc.cbSize        = sizeof(wc);
            wc.lpfnWndProc   = &wnd_proc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.lpszClassName = kClass;
            // RegisterClassExW may fail if already registered — that's OK.
            RegisterClassExW(&wc);

            // HWND_MESSAGE parent → message-only window (no visible
            // rendering, no owner, no Z-order presence).  Perfect for
            // receiving input events without any UI side-effects.
            HWND hwnd = CreateWindowExW(
                0, kClass, L"", 0,
                0, 0, 0, 0,
                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
            if (!hwnd) return;

            // Subscribe to raw keyboard.  Usage page 0x01 / usage 0x06
            // is the HID keyboard standard.  RIDEV_INPUTSINK asks for
            // events even when our window isn't in foreground.
            RAWINPUTDEVICE rid{};
            rid.usUsagePage = 0x01;
            rid.usUsage     = 0x06;
            rid.dwFlags     = RIDEV_INPUTSINK;
            rid.hwndTarget  = hwnd;
            if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
            {
                DestroyWindow(hwnd);
                return;
            }

            m_ready.store(true, std::memory_order_release);

            // Standard Windows message pump — dispatches WM_INPUT to
            // our WndProc.  GetMessage returns 0 on WM_QUIT (process
            // exit) or -1 on error; either way we break out.
            MSG msg;
            while (true)
            {
                BOOL r = GetMessageW(&msg, nullptr, 0, 0);
                if (r == 0 || r == -1) break;
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Cleanup on normal exit path (unusual — thread is detached
            // so this usually doesn't run).
            m_ready.store(false, std::memory_order_release);
            DestroyWindow(hwnd);
        }

        static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
        {
            if (msg == WM_INPUT)
            {
                UINT sz = 0;
                // First call: query required buffer size.
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                                    RID_INPUT, nullptr, &sz,
                                    sizeof(RAWINPUTHEADER)) == 0
                    && sz > 0 && sz <= sizeof(BYTE[128]))
                {
                    BYTE buf[128];
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),
                                        RID_INPUT, buf, &sz,
                                        sizeof(RAWINPUTHEADER)) == sz)
                    {
                        const auto* ri = reinterpret_cast<const RAWINPUT*>(buf);
                        if (ri->header.dwType == RIM_TYPEKEYBOARD)
                        {
                            const auto& kb = ri->data.keyboard;
                            // VKey 0xFF is "fake" — Windows sometimes
                            // sends it as padding.  Skip.
                            if (kb.VKey != 0xFF && kb.VKey < 256)
                            {
                                const bool isUp =
                                    (kb.Flags & RI_KEY_BREAK) != 0;
                                instance().m_down[kb.VKey]
                                    .store(!isUp, std::memory_order_release);
                            }
                        }
                    }
                }
                // WM_INPUT MUST call DefWindowProc per MSDN even after
                // we handle it — the OS performs cleanup in the default
                // handler.  Returning 0 alone leaks the raw-input data.
                return DefWindowProcW(hwnd, msg, wParam, lParam);
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        std::atomic<bool> m_ready{false};
        std::atomic<bool> m_down[256]{};
    };

    class FreeCamera
    {
    public:
        // APlayerCameraManager FCameraCacheEntry field offsets.
        // Verified via Ghidra:
        //   * APlayerCameraManager_TickAndCommitPOV @ 0x14203efe0 commits
        //     into this+0x410..+0x447 (the 5 NOP targets in CamLock).
        //   * APlayerCameraManager_GetCameraCacheLocation @ 0x142043210
        //     and _GetCameraCacheRotation @ 0x142043230 read from the
        //     same offsets.
        //   * APlayerController_GetPlayerViewPoint @ 0x142046410 reads
        //     these fields from PCM to fill the FMinimalViewInfo that
        //     ULocalPlayer::CalcSceneView consumes every frame.
        // DO NOT use these offsets on the ALuxBattleCamera actor — that's
        // a different object (ACameraActor subclass) whose +0x410 layout
        // is an unrelated director-scratch block nobody reads back.
        static constexpr std::ptrdiff_t kOffLocX   = 0x410;
        static constexpr std::ptrdiff_t kOffLocY   = 0x414;
        static constexpr std::ptrdiff_t kOffLocZ   = 0x418;
        static constexpr std::ptrdiff_t kOffPitch  = 0x41C;
        static constexpr std::ptrdiff_t kOffYaw    = 0x420;
        static constexpr std::ptrdiff_t kOffRoll   = 0x424;
        static constexpr std::ptrdiff_t kOffFov    = 0x428;

        // Turn free-camera ON / OFF.  On-transition captures the current
        // engine pose from `pcm` (the APlayerCameraManager) so there's
        // no visual jump on toggle.  Off-transition releases each CamLock
        // group iff we were the one that turned it on (so a user who
        // manually checked "Lock camera position" or "Lock camera
        // rotation" before enabling free-fly keeps their manual lock
        // when free-fly turns off).  Passing a null `pcm` on ON defers
        // the pose snapshot to the first tick() with a real pointer.
        //
        // `pcm` MUST be the APlayerCameraManager, not the
        // ALuxBattleCamera actor.  See the file-header comment for the
        // Ghidra trace that establishes which object the renderer reads
        // from.  dllmain.cpp resolves this via
        // PlayerController.PlayerCameraManager.
        //
        // We enable BOTH CamLock groups (primary + rotation) on ON
        // because Free-Fly needs both for full pose control:
        //   * Primary group: stops the engine from re-writing position
        //     every tick via the TickAndCommitPOV store row.
        //   * Rotation group: additionally stops the three external
        //     writers (rotSite1 whole-pose, rotSite2 follow-target,
        //     rotSite3 SetPOV-rotation) from re-stomping our rotation.
        //     Without rotSite1 in particular, arrow-key look input is
        //     silently overwritten and the camera appears un-rotatable.
        void set(bool on, CamLock& camLock, void* pcm)
        {
            if (on == m_enabled) return;
            m_enabled = on;
            if (on)
            {
                // Snapshot current pose so the toggle is visually smooth.
                // If pcm is null at toggle time (user pressed F7 before a
                // battle started), defer the snapshot to the first tick()
                // where we see a real pointer — otherwise we would write
                // the zeroed defaults (world origin, yaw 0) to
                // PCM+0x410..+0x428 on the first real tick and the user
                // would perceive "camera teleported to origin, stuck".
                if (pcm)
                {
                    auto* A = reinterpret_cast<uint8_t*>(pcm);
                    m_loc_x = readF(A + kOffLocX);
                    m_loc_y = readF(A + kOffLocY);
                    m_loc_z = readF(A + kOffLocZ);
                    m_pitch = readF(A + kOffPitch);
                    m_yaw   = readF(A + kOffYaw);
                    m_roll  = readF(A + kOffRoll);
                    m_fov   = readF(A + kOffFov);
                    m_need_snapshot = false;
                }
                else
                {
                    m_need_snapshot = true;
                }
                // Auto-enable both CamLock groups so the engine stops
                // overwriting us.  Track ownership per-group so we
                // only release the ones WE turned on; users who had
                // either group manually enabled keep their state when
                // free-fly flips off.
                if (!camLock.is_resolved()) camLock.resolve();
                const bool prim_was_on = camLock.is_enabled();
                const bool rot_was_on  = camLock.is_rotation_enabled();
                camLock.enable();
                camLock.enable_rotation_patches();
                m_owns_primary_lock  = !prim_was_on;
                m_owns_rotation_lock = !rot_was_on;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[Horse.FreeCamera] ON pcm=0x{:x} "
                        "loc=({}, {}, {}) rot=({}, {}, {}) fov={} "
                        "owns_prim={} owns_rot={}\n"),
                    reinterpret_cast<uintptr_t>(pcm),
                    m_loc_x, m_loc_y, m_loc_z,
                    m_pitch, m_yaw, m_roll, m_fov,
                    m_owns_primary_lock  ? 1 : 0,
                    m_owns_rotation_lock ? 1 : 0);
            }
            else
            {
                // Release each group we own, independently.  This
                // preserves any manual lock the user had engaged prior
                // to free-fly turning on.
                if (m_owns_primary_lock)
                {
                    camLock.disable();
                    m_owns_primary_lock = false;
                }
                if (m_owns_rotation_lock)
                {
                    camLock.disable_rotation_patches();
                    m_owns_rotation_lock = false;
                }
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[Horse.FreeCamera] OFF\n"));
            }
        }

        bool is_enabled() const { return m_enabled; }

        // Per-cockpit-tick update.  Polls keyboard + XInput pad 0 (if
        // game window is focused), applies user deltas to internal pose
        // state, and writes back to the camera manager's +0x410..+0x428
        // fields.  Safe to call with a null pcm (no-op).  `pcm` MUST
        // be the APlayerCameraManager; see file header.
        void tick(void* pcm)
        {
            if (!m_enabled || !pcm) return;
            if (!gameWindowFocused()) return;    // don't move while alt-tabbed

            // First-tick deferred snapshot: if set() was called before a
            // battle was loaded (pcm was null), capture the pose NOW so
            // we start from wherever the engine placed the camera instead
            // of blatting zeros into PCM+0x410..+0x428 (which would
            // teleport the camera to the world origin — the symptom the
            // user reported as "camera seems to be totally locked").
            if (m_need_snapshot)
            {
                auto* A0 = reinterpret_cast<uint8_t*>(pcm);
                m_loc_x = readF(A0 + kOffLocX);
                m_loc_y = readF(A0 + kOffLocY);
                m_loc_z = readF(A0 + kOffLocZ);
                m_pitch = readF(A0 + kOffPitch);
                m_yaw   = readF(A0 + kOffYaw);
                m_roll  = readF(A0 + kOffRoll);
                m_fov   = readF(A0 + kOffFov);
                m_need_snapshot = false;
                RC::Output::send<RC::LogLevel::Verbose>(
                    STR("[Horse.FreeCamera] deferred snapshot on first "
                        "non-null tick pcm=0x{:x} loc=({}, {}, {}) "
                        "rot=({}, {}, {}) fov={}\n"),
                    reinterpret_cast<uintptr_t>(pcm),
                    m_loc_x, m_loc_y, m_loc_z,
                    m_pitch, m_yaw, m_roll, m_fov);
            }

            // --- Memory-persistence diagnostic ----------------------------
            // At the START of every tick (before we write), read
            // camera+0x410..+0x428 and compare to what we wrote LAST
            // tick.  If the values differ, something in the game's
            // update chain wrote those fields between our ticks — so
            // we have a writer we haven't NOPed yet.  If the values
            // match but the user reports the camera doesn't visually
            // move, the renderer reads pose from somewhere OTHER than
            // this camera actor, and CamLock patches alone can't help.
            //
            // Logs once every ~2s while m_mem_diagnostic is on to
            // avoid spamming.  Enabled from the HUD's "Diagnostic:
            // verify memory writes" checkbox.
            if (m_mem_diagnostic && m_last_write_valid)
            {
                if (++m_mem_diag_counter >= 120)   // ~2s at 60Hz
                {
                    m_mem_diag_counter = 0;
                    auto* A = reinterpret_cast<uint8_t*>(pcm);
                    const float cur_lx = readF(A + kOffLocX);
                    const float cur_ly = readF(A + kOffLocY);
                    const float cur_lz = readF(A + kOffLocZ);
                    const float cur_p  = readF(A + kOffPitch);
                    const float cur_y  = readF(A + kOffYaw);
                    const float cur_r  = readF(A + kOffRoll);
                    const float cur_f  = readF(A + kOffFov);
                    const bool pose_persisted =
                        cur_lx == m_last_wrote_lx &&
                        cur_ly == m_last_wrote_ly &&
                        cur_lz == m_last_wrote_lz &&
                        cur_p  == m_last_wrote_p  &&
                        cur_y  == m_last_wrote_y  &&
                        cur_r  == m_last_wrote_r  &&
                        cur_f  == m_last_wrote_f;
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[Horse.FreeCamera.memdiag] pcm=0x{:x} "
                            "persisted={} "
                            "wrote=(L {},{},{}  R {},{},{}  F {})  "
                            "now=(L {},{},{}  R {},{},{}  F {})\n"),
                        reinterpret_cast<uintptr_t>(pcm),
                        pose_persisted ? 1 : 0,
                        m_last_wrote_lx, m_last_wrote_ly, m_last_wrote_lz,
                        m_last_wrote_p,  m_last_wrote_y,  m_last_wrote_r,
                        m_last_wrote_f,
                        cur_lx, cur_ly, cur_lz,
                        cur_p,  cur_y,  cur_r,
                        cur_f);
                    if (!pose_persisted)
                    {
                        // One writer stomped us — help the reader
                        // understand which field(s).
                        RC::Output::send<RC::LogLevel::Verbose>(
                            STR("[Horse.FreeCamera.memdiag]   "
                                "deltas: dLx={} dLy={} dLz={} "
                                "dP={} dY={} dR={} dFov={}\n"),
                            cur_lx - m_last_wrote_lx,
                            cur_ly - m_last_wrote_ly,
                            cur_lz - m_last_wrote_lz,
                            cur_p  - m_last_wrote_p,
                            cur_y  - m_last_wrote_y,
                            cur_r  - m_last_wrote_r,
                            cur_f  - m_last_wrote_f);
                    }
                }
            }

            // Arrow-key diagnostic: once every ~2s while free-cam is on,
            // print the state of each input source for the arrow cluster.
            // This helps localize where keys are (or aren't) visible.
            // Disabled by default; flip m_diagnostic on to enable.
            if (m_diagnostic)
            {
                if (++m_diag_counter >= 120)  // ~2s at 60Hz
                {
                    m_diag_counter = 0;
                    auto& ll  = LowLevelKeyInput::instance();
                    auto& rin = RawInputSource::instance();
                    // Per key: pair (LL hook / RawInput).  If both
                    // read 0 while the key is held, the driver-level
                    // event isn't reaching any of our subscribers —
                    // Steam Input or similar is eating it before
                    // delivery to our process.
                    RC::Output::send<RC::LogLevel::Verbose>(
                        STR("[Horse.FreeCamera.diag] "
                            "LL={} Raw={}  "
                            "UP: ll={} ri={}  "
                            "DN: ll={} ri={}  "
                            "L:  ll={} ri={}  "
                            "R:  ll={} ri={}  "
                            "W:  ll={} ri={}\n"),
                        ll.hook_installed() ? 1 : 0,
                        rin.ready()         ? 1 : 0,
                        ll.is_down(VK_UP)    ? 1 : 0, rin.is_down(VK_UP)    ? 1 : 0,
                        ll.is_down(VK_DOWN)  ? 1 : 0, rin.is_down(VK_DOWN)  ? 1 : 0,
                        ll.is_down(VK_LEFT)  ? 1 : 0, rin.is_down(VK_LEFT)  ? 1 : 0,
                        ll.is_down(VK_RIGHT) ? 1 : 0, rin.is_down(VK_RIGHT) ? 1 : 0,
                        ll.is_down('W')      ? 1 : 0, rin.is_down('W')      ? 1 : 0);
                }
            }

            // -- Controller state (player-0 gamepad).  ERROR_SUCCESS == 0.
            // If the pad isn't present xpState is left zeroed so all
            // stick/trigger/button reads are inert.
            //
            // Perf gate (2026-04): wrap the actual XInputGetState behind
            // controllerConnected()'s 1-second cache so we don't pay the
            // empty-slot stutter (1-10 ms per call) on every tick when
            // the user has free-fly on but no pad attached.  When a pad
            // IS connected the inner XInputGetState is fast (~µs), so
            // we still call it every tick to keep stick / trigger
            // responsiveness — only the no-pad path is throttled.
            XINPUT_STATE xpState{};
            const bool padOK = controllerConnected() &&
                (XInputGetState(0, &xpState) == ERROR_SUCCESS);
            const auto& g = xpState.Gamepad;

            // Modifier speed scalars — keyboard (Shift/Ctrl) OR controller
            // bumpers (RB fast, LB slow).  Combined multiplicatively is
            // surprising, so we pick the dominant one: any "fast" source
            // => 5×, otherwise any "slow" => 0.2×, else 1×.
            // Modifier-key polling — same two-source OR as the main
            // keyDown lambda below (re-declared here because the
            // lambda is scoped inside this function and is defined
            // later).
            auto& __llk_mods = LowLevelKeyInput::instance();
            auto& __raw_mods = RawInputSource::instance();
            const bool __ll_ok_mods  = __llk_mods.hook_installed();
            const bool __raw_ok_mods = __raw_mods.ready();
            auto __modDown = [&__llk_mods, &__raw_mods,
                              __ll_ok_mods, __raw_ok_mods](int vk) {
                if (__ll_ok_mods  && __llk_mods.is_down(vk)) return true;
                if (__raw_ok_mods && __raw_mods.is_down(vk)) return true;
                return false;
            };
            const bool kFast = __modDown(VK_SHIFT);
            const bool kSlow = __modDown(VK_CONTROL);
            const bool pFast = padOK &&
                               (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
            const bool pSlow = padOK &&
                               (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
            const bool fast = kFast || pFast;
            const bool slow = (kSlow || pSlow) && !fast;
            const float scale = fast ? 5.0f : (slow ? 0.2f : 1.0f);

            const float moveStep = m_move_speed * scale;   // cm per tick
            const float lookStep = m_look_speed * scale;   // deg per tick

            // Compute basis from current yaw/pitch.  UE4 convention:
            //   Forward = (cos(yaw)*cos(pitch), sin(yaw)*cos(pitch), sin(pitch))
            //   Right   = (-sin(yaw), cos(yaw), 0)
            const float kDeg2Rad = 3.14159265358979323846f / 180.0f;
            const float yawR   = m_yaw   * kDeg2Rad;
            const float pitR   = m_pitch * kDeg2Rad;
            const float cosY   = std::cos(yawR);
            const float sinY   = std::sin(yawR);
            const float cosP   = std::cos(pitR);
            const float sinP   = std::sin(pitR);
            const float fwdX   = cosY * cosP;
            const float fwdY   = sinY * cosP;
            const float fwdZ   = sinP;
            const float rgtX   = -sinY;
            const float rgtY   =  cosY;

            // Key polling: two independent sources OR-merged.  The
            // GetAsyncKeyState fallback was removed on request — it
            // can't see arrow keys in this SC6 build anyway (they're
            // filtered below the OS async state), so keeping it
            // masked legitimate-failure diagnostics without adding
            // real coverage.
            //
            //   1. LowLevelKeyInput (WH_KEYBOARD_LL)   — kernel driver
            //   2. RawInputSource   (RIDEV_INPUTSINK)  — HID device
            auto& llk = LowLevelKeyInput::instance();
            auto& raw = RawInputSource::instance();
            const bool ll_ok  = llk.hook_installed();
            const bool raw_ok = raw.ready();
            auto keyDown = [&llk, &raw, ll_ok, raw_ok](int vk) {
                if (ll_ok  && llk.is_down(vk)) return true;
                if (raw_ok && raw.is_down(vk)) return true;
                return false;
            };

            // -- Translation: sum keyboard + controller contributions.
            float moveFwd   = 0.0f;  // +forward / -back
            float moveRight = 0.0f;  // +right / -left
            float moveUp    = 0.0f;  // +up / -down
            if (keyDown('W')) moveFwd   += 1.0f;
            if (keyDown('S')) moveFwd   -= 1.0f;
            if (keyDown('D')) moveRight += 1.0f;
            if (keyDown('A')) moveRight -= 1.0f;
            if (keyDown('E')) moveUp    += 1.0f;
            if (keyDown('Q')) moveUp    -= 1.0f;

            if (padOK)
            {
                // Left stick: forward/strafe.  Normalise to [-1, 1] after
                // deadzone; thumb values range [-32768, 32767].
                float lx = normaliseStick(g.sThumbLX,
                                          XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                float ly = normaliseStick(g.sThumbLY,
                                          XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
                moveRight += lx;
                moveFwd   += ly;

                // Triggers: LT = down, RT = up.  [0, 255] with threshold.
                if (g.bLeftTrigger  > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
                    moveUp -= (g.bLeftTrigger  / 255.0f);
                if (g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD)
                    moveUp += (g.bRightTrigger / 255.0f);
            }

            m_loc_x += fwdX * moveFwd   * moveStep
                     + rgtX * moveRight * moveStep;
            m_loc_y += fwdY * moveFwd   * moveStep
                     + rgtY * moveRight * moveStep;
            m_loc_z += fwdZ * moveFwd   * moveStep
                     + moveUp           * moveStep;

            // -- Rotation: sum keyboard + controller contributions.
            float lookPitch = 0.0f;   // + up
            float lookYaw   = 0.0f;   // + right (clockwise from above)
            // Both arrow keys and IJKL are accepted.  Note: in some SC6
            // builds / states the arrow keys may not register via
            // GetAsyncKeyState because SC6 registers them with the
            // RawInput API's RIDEV_NOLEGACY flag (common pattern for
            // fighting games to reduce menu-input latency), which
            // suppresses the OS-level async key state.  IJKL uses
            // ordinary letter VKs and always registers regardless.
            // If the arrows feel dead, use IJKL as the reliable path.
            if (keyDown(VK_UP)    || keyDown('I')) lookPitch += 1.0f;
            if (keyDown(VK_DOWN)  || keyDown('K')) lookPitch -= 1.0f;
            if (keyDown(VK_LEFT)  || keyDown('J')) lookYaw   -= 1.0f;
            if (keyDown(VK_RIGHT) || keyDown('L')) lookYaw   += 1.0f;

            if (padOK)
            {
                float rx = normaliseStick(g.sThumbRX,
                                          XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                float ry = normaliseStick(g.sThumbRY,
                                          XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
                lookYaw   += rx;
                lookPitch += ry;
            }

            m_pitch += lookPitch * lookStep;
            m_yaw   += lookYaw   * lookStep;

            // Clamp pitch to avoid gimbal flip; wrap yaw to [-180, 180).
            if (m_pitch >  89.0f) m_pitch =  89.0f;
            if (m_pitch < -89.0f) m_pitch = -89.0f;
            if (m_yaw   >  180.0f) m_yaw -= 360.0f;
            if (m_yaw   < -180.0f) m_yaw += 360.0f;

            // Commit.
            auto* A = reinterpret_cast<uint8_t*>(pcm);
            writeF(A + kOffLocX,  m_loc_x);
            writeF(A + kOffLocY,  m_loc_y);
            writeF(A + kOffLocZ,  m_loc_z);
            writeF(A + kOffPitch, m_pitch);
            writeF(A + kOffYaw,   m_yaw);
            writeF(A + kOffRoll,  m_roll);
            writeF(A + kOffFov,   m_fov);

            // Remember what we just wrote so the next tick's memory-
            // persistence diagnostic can tell if something stomped us
            // in between.
            m_last_wrote_lx    = m_loc_x;
            m_last_wrote_ly    = m_loc_y;
            m_last_wrote_lz    = m_loc_z;
            m_last_wrote_p     = m_pitch;
            m_last_wrote_y     = m_yaw;
            m_last_wrote_r     = m_roll;
            m_last_wrote_f     = m_fov;
            m_last_write_valid = true;
        }

        // Speed and FOV accessors for UI sliders.  Units: cm/tick and
        // deg/tick (60Hz implied).  Default ≈ 20 cm/tick ≈ 12 m/s and
        // 1.5 deg/tick ≈ 90 deg/s — gentle but useful.
        float& move_speed() { return m_move_speed; }
        float& look_speed() { return m_look_speed; }
        float& fov_deg()    { return m_fov; }

        // Read-only pose accessors for HUD display.
        float loc_x() const { return m_loc_x; }
        float loc_y() const { return m_loc_y; }
        float loc_z() const { return m_loc_z; }
        float pitch() const { return m_pitch; }
        float yaw()   const { return m_yaw; }
        float roll()  const { return m_roll; }

        // Returns true iff the XInput pad at slot 0 is currently
        // connected.  Useful for a "controller detected" status line.
        //
        // Cached probe (perf audit, 2026-04).  XInputGetState on an
        // EMPTY slot is famously slow on Windows — 1-10 ms per call as
        // it spins the device-arrival query.  This getter is hit from
        // two hot paths:
        //   * The Camera HUD tab (every ImGui render frame while open).
        //   * tick() below (every cockpit tick while free-fly is on).
        // A user with no controller attached therefore eats per-render
        // stutter from the empty-slot poll, even if they never touch
        // free-fly.  We cache the answer for ~1 s so subsequent calls
        // are a single time_point compare + bool read (~ns).  Hot-plug
        // detection lag is up to ~1 s, which is fine for a status
        // indicator and an opt-in feature like free-fly.
        //
        // Threading: caller invariant is "game thread only" — both
        // ImGui rendering (inside the cockpit hook callback) and
        // FreeCamera::tick() execute on the game thread.  The plain
        // statics below carry no atomics; benign even under a race
        // (worst case: one extra empty-slot probe).
        static bool controllerConnected()
        {
            using clock = std::chrono::steady_clock;
            static clock::time_point s_last_probe{};
            static bool              s_connected = false;

            const auto now = clock::now();
            if (now - s_last_probe < std::chrono::milliseconds(1000))
                return s_connected;

            s_last_probe = now;
            XINPUT_STATE s{};
            s_connected = (XInputGetState(0, &s) == ERROR_SUCCESS);
            return s_connected;
        }

    private:
        static float readF(const void* addr) {
            float v = 0.0f;
            std::memcpy(&v, addr, sizeof(v));
            return v;
        }
        static void writeF(void* addr, float v) {
            std::memcpy(addr, &v, sizeof(v));
        }

        // Deadzone-aware stick normalisation.  Raw range is [-32768, 32767];
        // we output [-1, 1] with the inner `dz` band mapped to 0 and a
        // linear remap of the outer band.  This gives the typical "stiff
        // centre, smooth push" feel and matches how most games handle
        // XInput sticks.
        static float normaliseStick(short raw, short dz)
        {
            float v = static_cast<float>(raw);
            const float fdz = static_cast<float>(dz);
            if (v >  fdz) return (v - fdz) / (32767.0f - fdz);
            if (v < -fdz) return (v + fdz) / (32768.0f - fdz);
            return 0.0f;
        }

        // Returns true iff the foreground window belongs to our process.
        // We compare PIDs rather than HWNDs because we don't carry the
        // game's top-level HWND around — UE4SS injects into the process
        // itself, so checking PID match is both correct and cheap.
        static bool gameWindowFocused() {
            HWND fg = GetForegroundWindow();
            if (!fg) return false;
            DWORD fgPid = 0;
            GetWindowThreadProcessId(fg, &fgPid);
            return fgPid == GetCurrentProcessId();
        }

        bool  m_enabled = false;
        // True when set() was called with a null pcm (user toggled F7
        // before a battle loaded).  tick() takes the snapshot on its
        // first call with a non-null pcm and clears the flag.  Prevents
        // the "first real tick writes zeroed defaults to PCM+0x410..+0x428,
        // camera teleports to world origin" failure mode.
        bool  m_need_snapshot = false;
        // True when this class is the one that currently has the
        // PRIMARY CamLock group turned on.  Set in set(true) iff the
        // primary group wasn't already enabled when free-fly turned
        // on; cleared in set(false) after we release.  Prevents the
        // "turning free-fly off releases the user's manual Lock
        // camera position" misfire.
        bool  m_owns_primary_lock  = false;
        // Same for the ROTATION CamLock group.  Tracked separately
        // because the user can manually engage either group from the
        // HUD independently and we should preserve their choice.
        bool  m_owns_rotation_lock = false;
        float m_loc_x = 0.0f, m_loc_y = 0.0f, m_loc_z = 0.0f;
        float m_pitch = 0.0f, m_yaw   = 0.0f, m_roll  = 0.0f;
        float m_fov   = 70.0f;

        float m_move_speed = 20.0f;   // cm per cockpit tick (≈12 m/s at 60Hz)
        float m_look_speed = 1.5f;    // deg per cockpit tick (≈90 deg/s at 60Hz)

    public:
        // Diagnostic flag — when true, tick() prints arrow-key state
        // from both the LL hook and GetAsyncKeyState every ~2s.  Used
        // to narrow down why arrow keys may not be registering.  Toggle
        // via the HUD check-box.
        std::atomic<bool> m_diagnostic{false};

        // Memory-persistence diagnostic.  When true, tick() reads back
        // camera+0x410..+0x428 at the start of each tick and compares
        // to what we wrote LAST tick.  A mismatch means some code
        // between our ticks is overwriting the camera's pose fields —
        // which tells us there's another writer we haven't NOPed.
        // A persistent match that still doesn't move the camera
        // visually means the renderer reads from a different cache.
        std::atomic<bool> m_mem_diagnostic{false};

    private:
        int m_diag_counter = 0;
        int m_mem_diag_counter = 0;

        // Last-write tracking for m_mem_diagnostic.
        float m_last_wrote_lx = 0.0f, m_last_wrote_ly = 0.0f,
              m_last_wrote_lz = 0.0f;
        float m_last_wrote_p  = 0.0f, m_last_wrote_y  = 0.0f,
              m_last_wrote_r  = 0.0f, m_last_wrote_f  = 0.0f;
        bool  m_last_write_valid = false;
    };

} // namespace Horse
