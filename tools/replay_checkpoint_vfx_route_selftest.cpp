#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>
enum class FailureCode{CapacityExceeded};
struct Status{static Status failure(FailureCode){return {};}};
struct Birth{};
static const Birth* observed;
static std::size_t observed_companions;
static Status CaptureReplayVfx(int&,std::uintptr_t,void*,std::size_t,std::span<void* const> companions,const Birth* quarantine=nullptr) {
    observed=quarantine;observed_companions=companions.size();return {};
}
struct Host {
    bool corrected_capture_{};
    struct Historical{Birth birth;};std::unique_ptr<Historical> historical_restore_;
    struct Output{int vfx{};} output;
    Status status;std::uintptr_t image_base_=1;void* manager_=reinterpret_cast<void*>(2);
    std::array<void*,4> physics_owners{};std::size_t physics_owner_count=3;
    std::size_t participant_budget(){return 1024*1024;}
    Status captured(Status s,const wchar_t*){return s;}
    void Capture() {
#include "checkpoint_vfx_route.inl"
    }
};
int main() {
    Host h;h.Capture();assert(!observed && observed_companions==3);
    h.historical_restore_=std::make_unique<Host::Historical>();
    h.Capture();assert(!observed); // Complete-B acquisition keeps normal inventory.
    h.corrected_capture_=true;h.Capture();assert(observed==&h.historical_restore_->birth);
    h.historical_restore_.reset();h.Capture();assert(!observed);
}
