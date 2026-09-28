#include <array>
#include <cstdint>
#include <cstdio>
struct Fixture {
 bool coherence_=true,accepting_=true,coherence_pending_=false,rolling_coherence_=true;
 unsigned error_{},coherence_count_{};std::array<std::uint64_t,8> coherence_seen_{};
 void Capture(unsigned tick,unsigned generation,bool held){
#include "coherence_admission.inl"
  coherence_seen_[coherence_count_++]=key;
 }
};
int main(){
 Fixture f;f.Capture(190,0,false);
 for(unsigned g=0;g<30;++g){f.Capture(214,g,true);f.Capture(217,g,true);f.Capture(230,g,false);}
 f.Capture(300,30,false);
 if(f.error_ || f.coherence_count_!=5){std::puts("rolling visits exhausted bounded coherence slots");return 1;}
 Fixture legacy;legacy.rolling_coherence_=false;
 for(unsigned g=0;g<9;++g)legacy.Capture(230,g,false);
 if(!legacy.error_ || legacy.coherence_count_!=8)return 2;
}
