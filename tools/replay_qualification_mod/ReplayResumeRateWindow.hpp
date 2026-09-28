#pragma once
#include <cstdint>

namespace Horse::Qualification {
// Contiguous wall time, including polls with zero tick progress. Finish at
// the first observed boundary reaching the requested minimum; never invent
// an interpolated timestamp for ticks inside a multi-tick poll.
class ReplayResumeRateWindow {
    std::uint32_t first_{},last_{},minimum_{};
    std::uint64_t started_{},last_time_{},ticks_{},elapsed_{};
public:
    void Begin(std::uint32_t frame,std::uint64_t time_us,std::uint32_t minimum) {
        first_=last_=frame;started_=last_time_=time_us;minimum_=minimum;ticks_=elapsed_=0;
    }
    bool Sample(std::uint32_t frame,std::uint64_t time_us) {
        if(!minimum_ || frame<last_ || time_us<last_time_)return false;
        if(ticks_<minimum_) {
            ticks_=std::uint64_t(frame)-first_;
            elapsed_=time_us-started_;
        }
        last_=frame;last_time_=time_us;return true;
    }
    std::uint64_t ticks() const {return ticks_;}
    std::uint64_t elapsed_us() const {return elapsed_;}
};
}
