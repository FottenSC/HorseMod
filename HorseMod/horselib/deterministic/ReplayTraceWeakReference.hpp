#pragma once
#include <cstdint>
#include <limits>
#include <array>
#include <cstddef>

namespace Horse::Deterministic {
// Metadata-only handoff. Validate every row before changing any bookkeeping;
// no native weak count or ownership is transferred by this operation.
template<class Control,std::size_t N,class HasImageLease,class RetainedLiveOwner>
bool ReplayTraceReopenControls(std::array<Control,N>& controls,std::size_t count,
    std::size_t original_count,HasImageLease has_image_lease,RetainedLiveOwner retained_live_owner) {
    if(original_count>count || count>N)return false;
    for(std::size_t i=0;i<count;++i) {
        const auto& c=controls[i];
        if(i>=original_count) {
            if(c.removed || c.weak<1 || (c.strong==0 ? !has_image_lease(c)
                : c.strong<0 || !retained_live_owner(c)))return false;
        } else if(has_image_lease(c) && c.weak<=1)return false;
    }
    for(std::size_t i=0;i<original_count;++i)
        if(has_image_lease(controls[i]))--controls[i].weak;
    for(std::size_t i=original_count;i<count;++i)controls[i]={};
    return true;
}
template<class Control,std::size_t N,class HasImageLease>
bool ReplayTraceReopenControls(std::array<Control,N>& controls,std::size_t count,
    std::size_t original_count,HasImageLease has_image_lease) {
    return ReplayTraceReopenControls(controls,count,original_count,has_image_lease,
        [](const auto&){return false;});
}
// Native shared controller header only. Expired state at controller+16 is
// destroyed and must never be read, copied, or made strong by these operations.
struct ReplayTraceWeakController {
    std::uintptr_t type{};
    std::int32_t strong{}, weak{};
    bool Expired(std::uintptr_t expected_type) const noexcept {
        return type==expected_type && strong==0 && weak>0;
    }
    bool MatchesOwnership(std::uintptr_t expected_type, bool expired_lease,
        bool live_in_a, bool live_in_b) const noexcept {
        return type==expected_type && weak>0 &&
            (strong==0 ? expired_lease : strong>0 && live_in_a && live_in_b);
    }
    bool RetainExpired(std::uintptr_t expected_type) noexcept {
        if(!Expired(expected_type) || weak==std::numeric_limits<std::int32_t>::max()) return false;
        ++weak;return true;
    }
    // A fresh owner's operation may retain controller identity before native
    // execution. This never acquires strong ownership or delays logical death.
    bool RetainLiveIdentity(std::uintptr_t expected_type) noexcept {
        if(type!=expected_type || strong<=0 || weak<=0
            || weak==std::numeric_limits<std::int32_t>::max())return false;
        ++weak;return true;
    }
    bool ReleaseIdentity(std::uintptr_t expected_type,void(*delete_controller)(void*)) noexcept {
        if(type!=expected_type || strong<0 || weak<=0 || !delete_controller
            || (strong>0 && weak<2))return false;
        // A live controller retains its implicit native weak reference. After
        // native state destruction only the controller header may be touched.
        if(--weak==0)delete_controller(this);
        return true;
    }
    // The caller owns one weak reference and supplies native controller-only
    // deletion. The destroyed trace state's destructor is never invoked again.
    bool ReleaseExpired(std::uintptr_t expected_type,void(*delete_controller)(void*)) noexcept {
        if(!Expired(expected_type) || !delete_controller) return false;
        if(--weak==0) delete_controller(this);
        return true;
    }
};
static_assert(sizeof(ReplayTraceWeakController)==16);
}
