#pragma once
#include <cstdint>
#include <cmath>
namespace Horse::Qualification {
// Diagnostic setup only. The backend invokes the engine's real loading API;
// expected replay observations never enter this policy.
struct ReplayStartupLoading {
    bool busy{},complete{};
    std::uint32_t calls{};
    const char* failure{};
    static constexpr float registration_budget_ms=60000.0f;
    float previous_registration_budget{};
    bool registration_owned{},registration_restored{};
    std::uint32_t raw_checks{},raw_waits{};
    template<class Native> const char* AfterEngine(Native& native,bool eligible) {
        if(failure || complete || !eligible)return failure;
        const auto fail=[&](const char* reason){busy=false;
            failure=RestoreRegistration(native)?reason:"startup_registration_restore";return failure;};
        if(busy)return fail("startup_raw_reentry");
        if(!native.Owner() || !native.Signatures())return fail("startup_raw_binding");
        if(native.Tick()!=0)return nullptr;
        if(raw_checks==65536)return fail("startup_raw_capacity");
        ++raw_checks;busy=true;
        const auto started=native.Milliseconds();
        for(;;) {
            // Native callbacks remove completed raw asset groups under this
            // service lock. Backend releases the lock before any waiting.
            const auto pending=native.RawPending();
            if(pending==0){busy=false;return nullptr;}
            if(pending < -2 || pending == -1 || pending>4096)return fail("startup_raw_queue_invalid");
            if(native.Milliseconds()-started>=5000)return fail("startup_raw_timeout");
            native.WaitOne();++raw_waits;
            if(failure)return fail(failure);
            if(native.Tick()!=0)return fail("startup_raw_advanced_simulation");
        }
    }
    template<class Native> bool RestoreRegistration(Native& native) {
        if(!registration_owned)return true;
        if(!native.Owner() || !native.Signatures()
            || native.RegistrationBudget()!=registration_budget_ms)return false;
        native.RegistrationBudget()=previous_registration_budget;
        registration_owned=false;registration_restored=true;return true;
    }
    template<class Native> const char* Poll(Native& native, bool eligible) {
        if(failure || complete || !eligible)return failure;
        const auto fail=[&](const char* reason){
            failure=RestoreRegistration(native)?reason:"startup_registration_restore";return failure;};
        if(busy)return fail("startup_loading_reentry");
        if(!native.Owner() || !native.Signatures())return fail("startup_loading_binding");
        if(native.Tick()!=0){
            if(!RestoreRegistration(native))return fail("startup_registration_restore");
            complete=true;return nullptr;
        }
        if(!native.Ready())return nullptr;
        if(native.Suspended())return fail("startup_loading_suspended");
        // Native world streaming otherwise yields actor initialization on a
        // wall-clock budget while already-live stage particles can advance.
        // Raise only this setup budget; callbacks and native ordering remain
        // intact. Restore the exact predecessor at the first simulation tick.
        if(!registration_owned) {
            const auto previous=native.RegistrationBudget();
            if(!std::isfinite(previous) || previous<0 || previous>1000)
                return fail("startup_registration_budget_input");
            previous_registration_budget=previous;
            native.RegistrationBudget()=registration_budget_ms;registration_owned=true;
        } else if(native.RegistrationBudget()!=registration_budget_ms)
            return fail("startup_registration_budget_changed");
        if(!native.Pending())return nullptr;
        if(calls==64)return fail("startup_loading_capacity");
        busy=true;
        native.Flush(); // -1: native drain of all pending loads, including callbacks.
        busy=false;++calls;
        if(failure)return failure;
        if(native.Tick()!=0)return fail("startup_loading_advanced_simulation");
        if(native.Pending())return fail("startup_loading_incomplete");
        return nullptr;
    }
};
}
