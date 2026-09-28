// Exercise the actual post-VFX host capture boundary. Native receiver admission
// is controlled here; its routing/content checks are a separate fixture.
#include <cstdio>
#include <initializer_list>
struct Status {bool value;bool ok()const{return value;}};
struct Host {
    bool admitted{};unsigned calls{};
    bool capture_driving_{true},restore_preparation_driving_{};
    bool configuration_admitted{true};unsigned configuration_calls{};
    Status CaptureParticleConfigurations(int&) {++configuration_calls;return {configuration_admitted};}
    Status ValidateParticleCompletionOwnership(int&) {++calls;return {admitted};}
    Status ValidateParticleCompletionOwnership(int&,int&) {++calls;return {admitted};}
    Status CaptureExecution(bool retiring,bool prior_success) {
        int image{};struct {int traces;} execution{};Status status{prior_success};
#include "completion_execution_boundary.inl"
        return status;
    }
    Status Capture(bool complete,bool prior_success) {
        const bool completed_application=complete;int output{};Status status{prior_success};
        const auto captured=[](Status value,const wchar_t*){return value;};
#include "completion_capture_boundary.inl"
        return status;
    }
};
int main() {
    Host h;
    auto result=h.Capture(true,true);
    if(result.ok() || h.calls!=1) {std::puts("unowned completion receiver reached checkpoint sealing");return 1;}
    h.calls=0;h.admitted=true;
    if(!h.Capture(true,true).ok() || h.calls!=1)return 2;
    h.configuration_admitted=false;h.configuration_calls=0;
    if(h.Capture(true,true).ok() || h.configuration_calls!=1)return 8;
    h.restore_preparation_driving_=true;h.configuration_calls=0;
    if(!h.Capture(true,true).ok() || h.configuration_calls)return 9;
    h.restore_preparation_driving_=false;h.configuration_admitted=true;
    h.calls=0;
    if(h.Capture(true,false).ok() || h.calls)return 3;
    // This test does not confer completed-world ownership on interior captures.
    h.calls=0;h.Capture(false,true);if(h.calls)return 4;
    for(const bool retiring:{false,true}) {
        h.admitted=false;h.calls=0;
        if(h.CaptureExecution(retiring,true).ok() || h.calls!=1)return 5;
        h.admitted=true;h.calls=0;
        if(!h.CaptureExecution(retiring,true).ok() || h.calls!=1)return 6;
        h.calls=0;if(h.CaptureExecution(retiring,false).ok() || h.calls)return 7;
    }
    std::puts("completed A/B/C capture requires composed particle callback ownership");
}
