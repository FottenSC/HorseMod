#include "RollbackModuleUnload.hpp"

#include <cstdio>
#include <string_view>

int main()
{
    using Horse::ClassifyRollbackModuleUnload;
    using Horse::ClassifyRollbackModuleUnloadHostSupport;
    using Horse::RollbackModuleUnloadResult;
    using Horse::RollbackModuleUnloadResultName;

    const bool ready = ClassifyRollbackModuleUnload(
        false, false, false, true) == RollbackModuleUnloadResult::Ready;
    const bool callback = ClassifyRollbackModuleUnload(
        true, false, false, true)
        == RollbackModuleUnloadResult::BusyCallback;
    const bool transition = ClassifyRollbackModuleUnload(
        false, true, false, true)
        == RollbackModuleUnloadResult::BusyTransition;
    const bool deferred = ClassifyRollbackModuleUnload(
        false, false, true, true)
        == RollbackModuleUnloadResult::BusyTransition;
    const bool detach = ClassifyRollbackModuleUnload(
        false, false, false, false)
        == RollbackModuleUnloadResult::DetachFailed;
    const bool uncancellable_host_refuses_installed_hooks =
        ClassifyRollbackModuleUnloadHostSupport(true, false)
        == RollbackModuleUnloadResult::DetachFailed;
    const bool hook_free_unload_is_ready =
        ClassifyRollbackModuleUnloadHostSupport(false, false)
        == RollbackModuleUnloadResult::Ready;
    const bool cancellable_host_can_attempt_detach =
        ClassifyRollbackModuleUnloadHostSupport(true, true)
        == RollbackModuleUnloadResult::Ready;
    const bool names =
        std::string_view(RollbackModuleUnloadResultName(
            RollbackModuleUnloadResult::Ready)) == "ready"
        && std::string_view(RollbackModuleUnloadResultName(
            RollbackModuleUnloadResult::DetachFailed)) == "detach-failed";

    if (!(ready && callback && transition && deferred && detach
            && uncancellable_host_refuses_installed_hooks
            && hook_free_unload_is_ready
            && cancellable_host_can_attempt_detach
            && names))
    {
        std::fprintf(stderr,
            "rollback module unload self-test failed "
            "ready=%d callback=%d transition=%d deferred=%d detach=%d "
            "uncancellable=%d hook_free=%d cancellable=%d names=%d\n",
            ready, callback, transition, deferred, detach,
            uncancellable_host_refuses_installed_hooks,
            hook_free_unload_is_ready,
            cancellable_host_can_attempt_detach,
            names);
        return 1;
    }
    std::puts("rollback module unload self-test passed");
    return 0;
}
