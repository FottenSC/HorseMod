// Actual engine-world routing. The executor is an external continuation probe;
// its task admission/native completion remain covered by their own boundaries.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
namespace Horse::Deterministic {
template<class T>T& EngineField(void* p,std::size_t n){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+n);}
template<class R=void,class... A>R EngineNative(std::uintptr_t,std::uintptr_t rva,A...){
    if(rva==0x1f02230)throw std::runtime_error("RED context world enters native unowned GT pump");
    throw std::runtime_error("unexpected native service");
}
class Sc6ReplayHost {
public:
    struct Executor {
        bool active{};unsigned begins{},advances{};void* original_world{};
        bool idle()const{return !active;}
        void Begin(std::uintptr_t,void* world,int type,float delta){
            if(active || type!=2 || delta!=0.25f)throw std::runtime_error("bad executor entry");
            ++begins;original_world=world;active=true;
        }
        void Advance(){if(!active)throw std::runtime_error("idle executor advance");++advances;if(advances%2==0)active=false;}
    } executor_;
    std::uintptr_t image_base_{};void* engine_{};void* context_{};void* world_{};
    float engine_delta_=0.25f;
    bool idle_mode_{},paused_{},engine_world_bound_{};
    std::uint64_t held_updates_{},completed_worlds_{};
    bool TickEngineWorld();
};
#include "engine_world_body.inl"
}
int main(){
    using namespace Horse::Deterministic;
    std::array<std::byte,0x700> engine{};
    std::array<std::byte,0x300> context{};
    std::array<std::byte,8> bound{},other{},replacement{};
    Sc6ReplayHost host;host.engine_=engine.data();host.context_=context.data();host.world_=bound.data();
    try {
        // A paused bound world must not suppress another context's native work.
        host.paused_=true;EngineField<void*>(context.data(),0x298)=other.data();
        if(host.TickEngineWorld() || host.executor_.begins!=1 || host.executor_.original_world!=other.data())return 71;
        // Native world calls retain their argument despite context mutation.
        EngineField<void*>(context.data(),0x298)=bound.data();
        if(!host.TickEngineWorld() || host.executor_.begins!=1 || host.executor_.original_world!=other.data()
            || host.completed_worlds_ || host.held_updates_)return 72;
        if(!host.TickEngineWorld() || host.executor_.begins!=1 || host.held_updates_!=1)return 73;
        host.paused_=false;
        if(host.TickEngineWorld() || host.executor_.begins!=2 || host.executor_.original_world!=bound.data())return 74;
        EngineField<void*>(context.data(),0x298)=replacement.data();
        if(!host.TickEngineWorld() || host.completed_worlds_!=1 || host.executor_.begins!=2)return 75;
        host.idle_mode_=true;
        if(!host.TickEngineWorld() || host.executor_.begins!=2 || host.completed_worlds_!=1)return 76;
        std::puts("all context worlds retain admitted executor routing and bound-only pause/accounting PASS");
        return 0;
    }catch(const std::runtime_error& error){std::puts(error.what());return 81;}
}
