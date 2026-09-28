#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstdio>
enum class LogLevel {Default};
struct Output {template<LogLevel,class...T>static void send(T...) {}};
namespace RC {template<class T>const T& to_generic_string(const T& v){return v;}}
#define STR(s) s
int main(){
 bool interior_completed_=false,interior_started_=true,historical_operation_started_=true;
 bool world_pause_completed_=false,world_resume_measured_=false;
 std::uint64_t world_resume_frame_{},world_resume_max_gap_us_{},world_resume_late_gaps_{},world_resume_viewport_{};
 std::atomic<std::uint64_t> viewport_frame_count_{30};
 std::chrono::steady_clock::time_point world_resume_started_,world_resume_previous_;
 struct {unsigned tick=246;} held;
 struct {const char* run_id="fixture";}request_;
#include "rolling_resume.inl"
 for(unsigned offset : {119u,120u}) {
  struct {std::uint64_t frame;} sample{held.tick+offset};
  const auto timing_now=std::chrono::steady_clock::now();
#include "rolling_measure.inl"
  if(world_resume_measured_!=(offset==120)){std::puts("rolling continuation measurement did not follow 120 actual native ticks");return 1;}
 }
 if(!interior_completed_ || interior_started_ || historical_operation_started_)return 2;
}
