#pragma once
#include <cstdint>
#include <limits>

namespace Horse::GameImGui {
// Render-thread draft only. Submitting still uses the host's ordinary queued
// seek admission; editing this value cannot change replay or input history.
class ReplayTickEntry {
    std::uint64_t value_{};
    bool empty_{true};
public:
    void clear() noexcept {value_=0;empty_=true;}
    void reset(std::uint64_t tick) noexcept {value_=tick;empty_=false;}
    bool append(unsigned digit) noexcept {
        if(digit>9 || value_>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return false;
        value_=value_*10+digit;empty_=false;return true;
    }
    void erase() noexcept {if(value_<10)clear();else value_/=10;}
    bool empty() const noexcept {return empty_;}
    std::uint64_t value() const noexcept {return value_;}
    bool valid(std::uint64_t last) const noexcept {return !empty_ && value_<=last;}
};
}
