#pragma once
#include <array>
#include <span>
#include <cstddef>

namespace Horse::Deterministic {
// One seek owns the admission exclusion. Entries are process-local bindings;
// the HUD transaction separately leases and validates their UObject identities.
// No animation clocks, pending callbacks or expected observations are installed.
class ReplayHudWidgetAdmission final {
    const void* owner_{};
    std::array<const void*,16> widgets_{};
    std::size_t count_{};
public:
    bool Acquire(const void* owner,std::span<const void* const> widgets) {
        if(!owner || widgets.size()>widgets_.size())return false;
        if(owner_) {
            if(owner_!=owner || widgets.size()!=count_)return false;
            for(std::size_t i=0;i<count_;++i)if(widgets_[i]!=widgets[i])return false;
            return true;
        }
        for(std::size_t i=0;i<widgets.size();++i) {
            if(!widgets[i])return false;
            for(std::size_t j=0;j<i;++j)if(widgets[i]==widgets[j])return false;
        }
        for(std::size_t i=0;i<widgets.size();++i)widgets_[i]=widgets[i];
        count_=widgets.size();owner_=owner;return true;
    }
    bool Release(const void* owner) {
        if(!owner)return false;
        if(!owner_)return true;
        if(owner_!=owner)return false;
        widgets_={};count_=0;owner_=nullptr;return true;
    }
    bool Excludes(const void* widget) const {
        for(std::size_t i=0;i<count_;++i)if(widgets_[i]==widget)return true;
        return false;
    }
    bool OwnedBy(const void* owner) const {return owner_ && owner_==owner;}
    bool empty() const {return !owner_;}
};
inline ReplayHudWidgetAdmission replay_hud_widget_admission;
}
