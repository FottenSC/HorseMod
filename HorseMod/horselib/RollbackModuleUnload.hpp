#pragma once

#include <cstdint>

namespace Horse
{
    enum class RollbackModuleUnloadResult : uint8_t
    {
        Ready,
        BusyCallback,
        BusyTransition,
        DetachFailed,
    };

    constexpr const char* RollbackModuleUnloadResultName(
        RollbackModuleUnloadResult result) noexcept
    {
        switch (result)
        {
        case RollbackModuleUnloadResult::Ready: return "ready";
        case RollbackModuleUnloadResult::BusyCallback:
            return "busy-callback";
        case RollbackModuleUnloadResult::BusyTransition:
            return "busy-transition";
        case RollbackModuleUnloadResult::DetachFailed:
            return "detach-failed";
        }
        return "unknown";
    }

    constexpr RollbackModuleUnloadResult ClassifyRollbackModuleUnload(
        bool current_thread_inside_callback,
        bool transition_held,
        bool shutdown_deferred,
        bool detach_succeeded) noexcept
    {
        if (current_thread_inside_callback)
            return RollbackModuleUnloadResult::BusyCallback;
        if (transition_held || shutdown_deferred)
            return RollbackModuleUnloadResult::BusyTransition;
        return detach_succeeded
            ? RollbackModuleUnloadResult::Ready
            : RollbackModuleUnloadResult::DetachFailed;
    }

    // UE4SS's current uninstall callback cannot cancel the subsequent
    // FreeLibrary call. Once a native detour has been published, a callback
    // can already be executing between the patched entry point and our
    // admission gate. Refuse physical unload in that host instead of racing
    // trampoline destruction; dllmain pins the module and requires restart.
    constexpr RollbackModuleUnloadResult
    ClassifyRollbackModuleUnloadHostSupport(
        bool rollback_hooks_installed,
        bool host_can_cancel_unload) noexcept
    {
        return rollback_hooks_installed && !host_can_cancel_unload
            ? RollbackModuleUnloadResult::DetachFailed
            : RollbackModuleUnloadResult::Ready;
    }
}
