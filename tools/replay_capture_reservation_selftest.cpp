// Actual SealCapture with controlled COM/native lease backends. This tests
// which owner charges the history reservation, not GPU/native completion.
#define NOMINMAX
#include <Windows.h>
#include <array>
#include <memory>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "deterministic/ReplayCaptureAccounting.hpp"
#include "deterministic/ReplayStaticVectorField.hpp"
#define STR(x) x
#define REQUIRE(x) do {if(!(x)){std::printf("failure line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace RC {enum class LogLevel{Warning,Default};template<class T>T to_generic_string(T x){return x;}struct Output{template<LogLevel,class... T>static void send(const char*,T...) {}};}
using Horse::Deterministic::ReplayCaptureAccounting;
constexpr std::size_t image_bytes=56*1024*1024;
constexpr std::array<unsigned,6> pixel_bytes{16,8,16,8,4,4};
struct ReplayGpuImageEquality{static constexpr unsigned readback_bytes=4*4097*4;};
struct Texture {void* pointer{};void* Get()const{return pointer;}explicit operator bool()const{return pointer!=nullptr;}};
struct Sc6ReplayParticleCopy {
    struct PackedImage{bool completed{};std::size_t bytes{};bool ready()const{return completed;}std::size_t owned_bytes()const{return bytes;}};
    using Packed=std::array<std::shared_ptr<PackedImage>,4>;
    Packed packed_images_;
    enum class Phase {Empty,ReadyA};
    struct Witness {std::size_t bytes{},readback_maps{},immutable_image_shared_bytes{};unsigned immutable_image_shared_mask{};std::array<unsigned,4> immutable_image_nonuniform_tiles{};bool immutable_image_compared{},capture_sealed{};Phase phase=Phase::ReadyA;} witness_;
    struct CaptureIdentity {std::uint64_t session=1,tick=170,epoch=170;struct Source{bool active()const{return true;}}source;};
    struct History {
        unsigned offset{},pixel_bytes{};struct Desc{unsigned Width{},Height{},Format{};}descriptor;
        std::uintptr_t a{},b{},c{};Texture source_a,source_b,source_c,image_a,image_b,staging;
        std::uint64_t hash_a{};bool external{},copy_pixels{};
    };
    struct Uniform{void* value{};};struct Map{std::array<void*,3> installed{};};struct Allocation{void* installed{};};
    struct Lighting{bool captured=true;std::vector<Uniform> uniforms;std::array<Map,2> maps;std::vector<Allocation> allocations;}lighting_a_,lighting_b_;
    struct Visibility{bool captured=true;void* material_uniform{},*material_scene_uniform{};std::vector<void*>queries;std::size_t leases{};}visibility_a_;
    using LocalFields=Horse::Deterministic::ReplayStaticVectorFieldSet;
    LocalFields local_fields_a_,local_fields_b_;
    struct CapturedImage {
        LocalFields local_fields;
        CaptureIdentity identity;std::uintptr_t base{},world{},scene{},system{},pool{},view_state{};DWORD owner_thread{};HMODULE module{};
        std::size_t reservation{};ReplayCaptureAccounting::Reservation accounting;
        std::array<Texture,6> images,sources;std::array<void*,6> wrappers{};
        Packed packed;
        Lighting lighting;Visibility visibility;std::array<History,3>histories;Witness witness;
        static inline ReplayCaptureAccounting ledger;
        static inline std::weak_ptr<const CapturedImage> last_sealed;
        bool poisoned{};
        static inline std::vector<CapturedImage*> retired;
        static void Queue(const CapturedImage* image){retired.push_back(const_cast<CapturedImage*>(image));}
        static unsigned Drain(){unsigned count{};for(auto* p:retired){REQUIRE(ledger.Retire(p->accounting,[&]{delete p;}));++count;}retired.clear();return count;}
    };
    using CaptureHandle=std::shared_ptr<const CapturedImage>;
    CaptureHandle captured_;std::array<Texture,6>images_,sources_;std::array<void*,6>wrappers_{};std::array<History,3>histories_;
    std::uintptr_t base_=1,world_=2,scene_=3,system_=4,pool_=5,view_state_=6;
    struct Completion{bool done=true;bool retired()const{return done;}}completion_;
    bool clone_ok=true,bindings_ok=true;unsigned clones{};bool Bindings(void*,bool){return bindings_ok;}
    Sc6ReplayParticleCopy(){lighting_b_.captured=false;}
    bool CloneCaptureOwners(CapturedImage& image){++clones;for(std::size_t i=0;i<3;++i)image.histories[i].a=histories_[i].a;return clone_ok;}
    static std::size_t retained_capture_bytes(){return CapturedImage::ledger.bytes();}
    bool SealCapture(const CaptureIdentity&,std::size_t)noexcept;
    static std::size_t reopen_shared_bytes(const CaptureHandle&) noexcept;
    std::size_t shared_capture_bytes()const noexcept;
};
static BOOL TestModuleLease(DWORD,LPCWSTR,HMODULE* module){*module=reinterpret_cast<HMODULE>(1);return TRUE;}
#define GetModuleHandleExW TestModuleLease
#include "replay_capture_seal_method.inl"
#undef GetModuleHandleExW
int main(){
    using P=Sc6ReplayParticleCopy;
    P operation;
    operation.local_fields_a_.captured=true;operation.local_fields_a_.count=1;
    operation.local_fields_a_.rows[0].render=1234;operation.local_fields_b_.count=2;
    for(unsigned i=0;i<6;++i){operation.images_[i].pointer=reinterpret_cast<void*>(100+i);operation.sources_[i].pointer=reinterpret_cast<void*>(200+i);}
    {
        P packed;packed.images_=operation.images_;packed.sources_=operation.sources_;
        packed.packed_images_[0]=std::make_shared<P::PackedImage>();
        packed.packed_images_[0]->bytes=65536;
        packed.witness_.bytes=2*image_bytes+sizeof(P)+65536;
        REQUIRE(!packed.SealCapture({},512*1024*1024));
        REQUIRE(!packed.captured_ && P::retained_capture_bytes()==0);
        packed.packed_images_[0]->completed=true;
        REQUIRE(packed.SealCapture({},512*1024*1024));
        const auto reservation=packed.witness_.bytes+sizeof(P::CapturedImage)+4096-16*1024*1024;
        REQUIRE(packed.captured_->reservation==reservation);
        REQUIRE(!packed.captured_->images[0] && packed.captured_->packed[0]==packed.packed_images_[0]);
        REQUIRE(packed.shared_capture_bytes()==2*image_bytes-16*1024*1024+65536);
        REQUIRE(P::reopen_shared_bytes(packed.captured_)==packed.shared_capture_bytes());
        packed.captured_.reset();REQUIRE(P::CapturedImage::Drain()==1 && P::retained_capture_bytes()==0);
    }
    std::size_t history_gross{},history_B{};
    for(unsigned i=0;i<3;++i){
        auto& h=operation.histories_[i];h.offset=0xb60-i*0x20;h.a=300+i;
        h.descriptor={i==2?1u:1280u,i==2?1u:720u,1};h.pixel_bytes=i==1?4:8;
        h.copy_pixels=i!=0;h.source_a.pointer=reinterpret_cast<void*>(400+i);
        const auto bytes=std::size_t(h.descriptor.Width)*h.descriptor.Height*h.pixel_bytes;
        if(h.copy_pixels){h.image_a.pointer=reinterpret_cast<void*>(500+i);h.image_b.pointer=reinterpret_cast<void*>(600+i);}
        history_gross+=bytes*(h.copy_pixels?4:2)+2*0xb8;
        history_B+=bytes*(h.copy_pixels?2:1)+0xb8;
    }
    operation.witness_.bytes=2*image_bytes+sizeof(P)+history_gross+4096;
    const auto gross=operation.witness_.bytes+sizeof(P::CapturedImage)+4096;
    const auto before=operation.witness_.bytes;
    operation.bindings_ok=false;
    REQUIRE(!operation.SealCapture({},512*1024*1024));
    REQUIRE(!operation.clones && P::retained_capture_bytes()==0 && P::CapturedImage::retired.empty());
    operation.bindings_ok=true;
    REQUIRE(operation.SealCapture({},512*1024*1024));
    REQUIRE(operation.captured_->local_fields.captured && operation.captured_->local_fields.count==1);
    REQUIRE(operation.captured_->local_fields.rows[0].render==1234);
    const auto retained=P::retained_capture_bytes();
    std::printf("history reservation retained=%zu expected=%zu operation_B=%zu operation_gross=%zu\n",retained,gross-history_B,history_B,operation.witness_.bytes);
    REQUIRE(retained==gross-history_B);
    REQUIRE(operation.captured_->reservation==gross-history_B); // Only immutable ownership; reopening uses witness.bytes.
    REQUIRE(operation.captured_->witness.bytes==operation.witness_.bytes);
    {
        P second=operation;second.captured_.reset();
        REQUIRE(second.SealCapture({},512*1024*1024));
        std::size_t shared_histories{};
        for(const auto& h:operation.histories_)shared_histories+=std::size_t(h.descriptor.Width)*h.descriptor.Height*h.pixel_bytes;
        REQUIRE(P::retained_capture_bytes()==2*retained-2*image_bytes-shared_histories);
        second.captured_.reset();
        REQUIRE(P::CapturedImage::Drain()==1 && P::retained_capture_bytes()==retained);
    }
    for(unsigned i=0;i<3;++i){const auto& a=operation.captured_->histories[i];REQUIRE(!a.b && !a.source_b && !a.image_b && !a.c && !a.source_c);if(a.copy_pixels)REQUIRE(operation.histories_[i].image_b);}
    const auto shared=operation.shared_capture_bytes();
    REQUIRE(shared==P::reopen_shared_bytes(operation.captured_));
    REQUIRE(operation.witness_.bytes==before);
    REQUIRE(retained+before-shared==gross-history_B+before-shared);
    REQUIRE(before-shared>=history_B); // B remains covered while operation exists.
    auto saved=operation.captured_;
    operation.captured_.reset();
    REQUIRE(P::retained_capture_bytes()==retained); // Immutable lease still alive.
    REQUIRE(saved->witness.bytes==before); // Fresh B allocations use full envelope.
    saved.reset();
    REQUIRE(P::retained_capture_bytes()==retained); // Queue is not retirement.
    REQUIRE(P::CapturedImage::Drain()==1 && P::retained_capture_bytes()==0);
    REQUIRE(P::CapturedImage::Drain()==0);
    operation.clone_ok=false;
    REQUIRE(!operation.SealCapture({},512*1024*1024));
    REQUIRE(!operation.captured_ && P::retained_capture_bytes()==retained);
    REQUIRE(P::CapturedImage::Drain()==1 && P::retained_capture_bytes()==0);
    REQUIRE(P::CapturedImage::Drain()==0);
    operation.clone_ok=true;
    operation.histories_[0].b=999;
    const auto clones=operation.clones;
    REQUIRE(!operation.SealCapture({},512*1024*1024));
    REQUIRE(operation.clones==clones && P::retained_capture_bytes()==0);
    operation.histories_[0].b=0;
    operation.histories_[1].image_b.pointer=nullptr;
    REQUIRE(!operation.SealCapture({},512*1024*1024));
    REQUIRE(operation.clones==clones && P::CapturedImage::retired.empty());
    std::puts("Production capture-only reservation and preserved operation B envelope passed");
}
