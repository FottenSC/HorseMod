#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define STR(x) x
namespace Horse::Deterministic { enum class FailureCode {None,UnsupportedContent,Cancelled,CaptureFailed}; }
namespace RC { const char* to_generic_string(const char* p){return p;} }
enum class LogLevel {Default};
struct Output {template<LogLevel,class... T> static void send(const char*,T...) {}}
;
struct ReplayHost {using PauseMonitor=void(*)(void*,int);};
using Failure=Horse::Deterministic::FailureCode;
enum class Phase {AwaitingBoundary,Capturing,RetiringScratch,Resuming,Complete,Failed};
struct Index {enum class Phase {Recording,Complete,Failed};};
enum class IndexCapture {Capturing,Retained,Resuming};
static bool monitor_ok=true;static unsigned monitor_calls{};
bool release_monitor(void* p,ReplayHost::PauseMonitor m){++monitor_calls;return !p && !m && monitor_ok;}
template<class T>T ResolveHorseModExport(const char*){return reinterpret_cast<T>(&release_monitor);}
struct Fixture {
    struct {Failure failure{Failure::UnsupportedContent};Phase phase{Phase::Resuming};std::uint64_t capture_elapsed_us{},owned_bytes{};} work;
    struct {Index::Phase phase{Index::Phase::Recording};std::uint64_t entries{4001};} indexed;
    struct {bool index_cancel{};const char* run_id{"fixture"};} request_;
    IndexCapture index_capture_{IndexCapture::Capturing};std::uint64_t index_capture_tick_{4000};
    unsigned index_checkpoint_count_{2};bool failed{},fell_through{};
    void Fail(const char*){failed=true;}
    void Observe(){
#include "optional_checkpoint_observer.inl"
        fell_through=true;
    }
};
void require(bool v,int line){if(!v){std::fprintf(stderr,"optional checkpoint observer failed line %d\n",line);std::exit(1);}}
#define REQUIRE(v) require((v),__LINE__)
int main(){
    for(unsigned scenario=0;scenario<11;++scenario){
        Fixture f;monitor_ok=true;monitor_calls=0;
        if(scenario==1)f.work.phase=Phase::Capturing;
        if(scenario==2)f.work.capture_elapsed_us=1;
        if(scenario==3)f.work.owned_bytes=1;
        if(scenario==4)f.index_checkpoint_count_=0;
        if(scenario==5)f.index_capture_=IndexCapture::Retained;
        if(scenario==6)f.indexed.phase=Index::Phase::Failed;
        if(scenario==7)--f.indexed.entries;
        if(scenario==8)f.request_.index_cancel=true;
        if(scenario==9)f.work.failure=Failure::CaptureFailed;
        if(scenario==10)monitor_ok=false;
        f.Observe();
        if(!scenario){REQUIRE(!f.failed && !f.fell_through && monitor_calls==1 && f.index_capture_==IndexCapture::Resuming && f.index_checkpoint_count_==2);}
        else {REQUIRE(f.failed && !f.fell_through && monitor_calls==(scenario==10?1u:0u));}
    }
    std::puts("optional placement has no capture, no retained claim and explicit monitor release");
}
