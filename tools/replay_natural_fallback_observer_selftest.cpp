#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define STR(x) x
namespace Horse::Deterministic {enum class FailureCode {None,RestorePreflightFailed,GenerationMismatch};}
namespace RC {const char* to_generic_string(const char* p){return p;}}
enum class LogLevel {Default};
static unsigned receipts{};
struct Output {template<LogLevel,class... T>static void send(const char*,T...){++receipts;}};
using Failure=Horse::Deterministic::FailureCode;
enum class IndexPreparationTrial {Idle,FinalArmed};
struct Fixture {
    struct {bool index_preparation_fallback{};const char* run_id{"c"};} request_;
    struct {std::uint64_t checkpoint{2511},undo_tick{11148},rejected_checkpoint{};
        unsigned preparation_fallbacks{1},execution_fallbacks{};bool automatic_selection{true},retained_owner_rejected{};
        Failure last_preparation_failure{Failure::GenerationMismatch};} seek;
    std::uint64_t nearest{3989},preceding{2511};unsigned index_checkpoint_count_{5};
    IndexPreparationTrial index_preparation_trial_{IndexPreparationTrial::Idle};bool failed{},continued{};
    void Fail(const char*){failed=true;}
    void Observe(){
#include "natural_checkpoint_fallback.inl"
        continued=true;
    }
};
void require(bool value,int line){if(!value){std::fprintf(stderr,"natural fallback observer failed line %d\n",line);std::exit(1);}}
#define REQUIRE(v) require((v),__LINE__)
int main(){
    for(unsigned scenario=0;scenario<9;++scenario){
        Fixture f;receipts=0;
        if(scenario==1)f.seek.automatic_selection=false;
        if(scenario==2)f.seek.last_preparation_failure=Failure::None;
        if(scenario==3)f.seek.execution_fallbacks=1;
        if(scenario==4)f.seek.preparation_fallbacks=5;
        if(scenario==5){f.nearest=7963;f.preceding=6415;f.seek.retained_owner_rejected=true;f.seek.rejected_checkpoint=7963;}
        if(scenario==6)f.request_.index_preparation_fallback=true; // Forced protocol cannot use natural admission.
        if(scenario==7){f.request_.index_preparation_fallback=true;f.index_preparation_trial_=IndexPreparationTrial::FinalArmed;f.seek.last_preparation_failure=Failure::RestorePreflightFailed;}
        if(scenario==8){f.seek.preparation_fallbacks=0;f.seek.retained_owner_rejected=true;f.seek.rejected_checkpoint=3989;}
        f.Observe();
        if(scenario==0 || scenario>=5 && scenario!=6)REQUIRE(!f.failed && f.continued && receipts);
        else REQUIRE(f.failed && !f.continued && !receipts);
    }
}
