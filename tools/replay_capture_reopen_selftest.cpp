// Production capture admission, COM-identity accounting and lighting release
// methods, with local allocation/binding backends. No live GPU proof claimed.
#include <Windows.h>
#include <array>
#include <memory>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {enum class LogLevel {Warning,Default};struct Output {template<LogLevel,class... T>static void send(const char*,T...) {}};}
static unsigned field_reads{},frees{};
static void FreeTest(void*) {++frees;}
template<class T>T& Field(std::uintptr_t address,std::size_t offset=0) {++field_reads;return *reinterpret_cast<T*>(address+offset);}
struct Texture {
    void* pointer{};void* Get()const{return pointer;}explicit operator bool()const{return pointer!=nullptr;}
};
class Sc6ReplayTraceState;
struct Sc6ReplayParticleCopy {
    static constexpr std::size_t image_bytes=56*1024*1024;
    static constexpr std::array<unsigned,6> pixel_bytes{16,8,16,8,4,4};
    struct PackedImage{bool ready()const{return true;}std::size_t owned_bytes()const{return 65536;}};
    std::array<std::shared_ptr<PackedImage>,4> packed_images_;
    enum class Phase {Empty,ReadyA,Failed};
    struct Witness {std::size_t bytes{};bool diagnostic_readbacks{};Phase phase{};} witness_;
    struct History {struct Desc {unsigned Width=16,Height=16,Format=1;} descriptor;unsigned pixel_bytes=4;Texture source_a,image_a;};
    struct CapturedImage {
        bool poisoned{};struct {bool registered=true;} accounting;
        std::size_t reservation{};Witness witness;
        struct {std::uint64_t tick=170;} identity;
        std::uintptr_t base=1,world=2,view_state=3,scene=4,system=5,pool=6;
        DWORD owner_thread=GetCurrentThreadId();
        std::array<Texture,6> images,sources;std::array<History,3> histories;
        std::array<std::shared_ptr<PackedImage>,4> packed;
    };
    using CaptureHandle=std::shared_ptr<const CapturedImage>;
    CaptureHandle captured_;std::array<Texture,6> images_,sources_;std::array<History,3> histories_;
    std::uintptr_t base_{},world_{},view_state_{},scene_{},system_{},pool_{};
    bool binding_ok=true,load_ok=true,mutate_loaded=false;unsigned binds{},loads{};
    bool Bindings(void*,bool) {++binds;scene_=4;system_=5;pool_=6;return binding_ok;}
    bool LoadCaptureUnchecked(const CaptureHandle& image) {
        ++loads;if(!load_ok)return false;captured_=image;images_=image->images;sources_=image->sources;histories_=image->histories;
        if(mutate_loaded)images_[0].pointer=reinterpret_cast<void*>(99);
        witness_.phase=Phase::ReadyA;return true;
    }
    void Fail(HRESULT) {witness_.phase=Phase::Failed;}
    static std::size_t reopen_shared_bytes(const CaptureHandle&)noexcept;
    std::size_t shared_capture_bytes()const noexcept;
    bool BeginFromCapture(std::uintptr_t,void*,const CaptureHandle&,std::size_t,void*)noexcept;
    struct LightingPrimitive {enum class Binding {ScenePrimitive};Binding binding{};std::uintptr_t address{};};
    struct Allocation {void* installed{};std::uintptr_t address{};};
    struct Map {std::array<std::uintptr_t,12> header{};std::array<void*,3> installed{};};
    struct Uniform {void* value{};};
    struct Lighting {std::vector<Allocation> allocations;std::array<Map,2> maps;std::vector<Uniform> uniforms;std::vector<LightingPrimitive> primitives;} lighting_a_,lighting_b_;
    const Sc6ReplayTraceState* trace_render_owner_{};
    const void* fresh_trace_render_proof_{};
    std::array<int,16> fresh_trace_render_owners_{};
    std::size_t fresh_trace_render_count_{};
    const void* ground_render_owner_{};
    bool capture_owner_fault_{},lighting_executing_{},lighting_execution_settled_{},lighting_dirty_{},lighting_transferred_{};
    std::array<int,4> creation_render_owners_{};unsigned creation_render_owner_count_{};std::vector<int> stage_render_owners_;
    std::array<int,32> particle_render_owners_{};std::size_t particle_render_owner_count_{};bool particle_render_bound_{};
    bool LightingImageMatches(bool)const{return true;}
    bool LightingMapMatches(const Map&,std::uintptr_t,bool)const{return true;}
    bool FinishLighting(bool)noexcept;
};
#include "replay_capture_reopen_methods.inl"
#define LIGHTING_REJECT() return false
#include "replay_lighting_release_method.inl"
#undef LIGHTING_REJECT
int main() {
    using P=Sc6ReplayParticleCopy;
    auto image=std::make_shared<P::CapturedImage>();
    image->witness.bytes=2*P::image_bytes+16384;image->reservation=image->witness.bytes+4096;
    for(unsigned i=0;i<6;++i){image->images[i].pointer=reinterpret_cast<void*>(100+i);image->sources[i].pointer=reinterpret_cast<void*>(200+i);}
    for(unsigned i=0;i<3;++i){image->histories[i].source_a.pointer=reinterpret_cast<void*>(300+i);image->histories[i].image_a.pointer=reinterpret_cast<void*>(400+i);}
    const auto credit=P::reopen_shared_bytes(image);REQUIRE(credit==2*P::image_bytes+6144);
    const auto required=image->witness.bytes-credit;
    P rejected;REQUIRE(!rejected.BeginFromCapture(1,reinterpret_cast<void*>(2),image,required-1,reinterpret_cast<void*>(3)));
    REQUIRE(!rejected.binds && !rejected.loads && rejected.witness_.phase==P::Phase::Empty);
    field_reads=0;REQUIRE(rejected.FinishLighting(false) && !field_reads && !rejected.capture_owner_fault_);
    REQUIRE(rejected.FinishLighting(false) && !field_reads); // Repeat rejected-operation cleanup.
    P accepted;REQUIRE(accepted.BeginFromCapture(1,reinterpret_cast<void*>(2),image,required,reinterpret_cast<void*>(3)));
    REQUIRE(accepted.binds==1 && accepted.loads==1 && accepted.shared_capture_bytes()==credit);
    for(unsigned fault=0;fault<4;++fault) {
        auto bad=std::make_shared<P::CapturedImage>(*image);P operation;
        if(fault==0)bad->poisoned=true;
        if(fault==1)bad->owner_thread=GetCurrentThreadId()+1;
        if(fault==2)bad->view_state=9;
        if(fault==3)bad->accounting.registered=false;
        REQUIRE(!operation.BeginFromCapture(1,reinterpret_cast<void*>(2),bad,required,reinterpret_cast<void*>(3)));
        REQUIRE(!operation.loads);
    }
    P changed;changed.mutate_loaded=true;
    REQUIRE(!changed.BeginFromCapture(1,reinterpret_cast<void*>(2),image,required,reinterpret_cast<void*>(3)));
    REQUIRE(changed.witness_.phase==P::Phase::Failed); // Planned sharing never masks changed actual identities.
    P unbound;unbound.lighting_a_.allocations.push_back({reinterpret_cast<void*>(77),0});
    REQUIRE(!unbound.FinishLighting(false) && !frees && unbound.lighting_a_.allocations[0].installed);
    std::vector<unsigned char> scene(0xb00),primitive(0x100);std::array<std::uintptr_t,1> members{reinterpret_cast<std::uintptr_t>(primitive.data())};
    P owned;owned.base_=reinterpret_cast<std::uintptr_t>(&FreeTest)-0xd46a00;owned.scene_=reinterpret_cast<std::uintptr_t>(scene.data());
    Field<int>(owned.scene_,0xad0)=1;Field<std::uintptr_t>(owned.scene_,0xac8)=reinterpret_cast<std::uintptr_t>(members.data());
    Field<std::uintptr_t>(members[0],0xd8)=owned.scene_;Field<int>(members[0],0xe4)=0;Field<std::uintptr_t>(members[0],0x50)=77;
    owned.trace_render_owner_=reinterpret_cast<const Sc6ReplayTraceState*>(77);
    owned.ground_render_owner_=reinterpret_cast<const void*>(78);
    owned.lighting_a_.allocations.push_back({reinterpret_cast<void*>(77),0});
    REQUIRE(!owned.FinishLighting(false) && !frees && owned.lighting_a_.allocations[0].installed && owned.trace_render_owner_ && owned.ground_render_owner_);
    Field<std::uintptr_t>(members[0],0x50)=0;
    REQUIRE(owned.FinishLighting(false) && frees==1 && !owned.lighting_a_.allocations[0].installed && !owned.trace_render_owner_ && !owned.ground_render_owner_);
    owned.scene_=0;field_reads=0;REQUIRE(owned.FinishLighting(false) && !field_reads && frees==1);
    std::puts("Production reopen shared-memory admission and empty/installed lighting retirement passed");
}
