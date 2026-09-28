// Executes the production completion method with fixture-owned memory and a
// controlled native-call boundary. This proves retry/order/undo guards, not
// shipped component-job or GPU equivalence (those require the combat run).
namespace PublicationTailTest {
template<class T> const T& ReadAt(const void* p,std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p)+offset);
}
static bool signatures=true;
static bool CreationRenderFunctionsMatch(std::uintptr_t) {return signatures;}
struct Sc6ReplayWorldState {
    struct PreparedRestore {
        using RenderSet=std::array<std::byte,0x50>;
        enum class PublicationWork {Unavailable,HandedOff,Completing,Completed};
        struct Diagnostic {const char* check{};std::uintptr_t component{};std::uint64_t observed{},expected{};};
        Diagnostic render_diagnostic_{};
        bool render_executing_=true,render_execution_settled_=false,render_published_=true,render_undo_started_=false;
        PublicationWork render_publication_work_=PublicationWork::HandedOff;
        std::array<RenderSet,2> render_previous_{},render_publication_backing_{},render_owned_{};
        std::size_t owned_bytes_{},render_execution_budget_{};
        std::array<std::uint64_t,2> render_previous_fingerprint_{},render_publication_fingerprint_{};
        std::uintptr_t base_{};void* world_{};
        bool binding_valid=true,ownership_valid=true,leave_pending=false,change_B=false;
        mutable unsigned native_calls{};
        Status ValidateRenderWork() const {return ownership_valid?Status::success():Status::failure(FailureCode::GenerationMismatch);}
        Status BeginRenderWorkExecution(std::size_t) noexcept;
        Status ValidateBinding(std::uint64_t epoch) const {
            return binding_valid && epoch==77?Status::success():Status::failure(FailureCode::GenerationMismatch);
        }
        Status ValidateRenderWorkStorage(std::uint64_t epoch,const std::array<RenderSet,2>& owned,bool native_capacity) const {
            return ownership_valid && !native_capacity && epoch==77 && owned==render_publication_backing_
                ?Status::success():Status::failure(FailureCode::GenerationMismatch);
        }
        static bool FingerprintRenderSet(const RenderSet& image,std::uint64_t& hash) {
            if(ReadAt<int>(image.data(),8)<ReadAt<int>(image.data(),0x34))return false;
            hash=14695981039346656037ull;
            for(auto value:image){hash^=std::to_integer<unsigned char>(value);hash*=1099511628211ull;}
            return true;
        }
        void InvokeNativePublicationRenderTail() const noexcept {
            ++native_calls;
            if(!leave_pending) for(unsigned i=0;i<2;++i) {
                auto* data=static_cast<std::byte*>(world_)+0x1d0+i*0x50;
                const auto count=ReadAt<int>(data,8);std::memcpy(data+0x34,&count,4);
            }
            if(change_B)const_cast<PreparedRestore*>(this)->render_previous_[0][0]=std::byte{9};
        }
        Status CompleteUnexecutedPublicationRenderWork() noexcept;
    };
};
#include "../HorseMod/horselib/deterministic/Sc6ReplayWorldState.RenderHandoff.inl"
#include "../HorseMod/horselib/deterministic/Sc6ReplayWorldState.PublicationTail.inl"
static void Run() {
    using Restore=Sc6ReplayWorldState::PreparedRestore;
    // Reserve virtual addresses matching the production RVAs, committing only
    // the two data pages read by the actual method. No native game is loaded.
    auto* base=static_cast<std::byte*>(VirtualAlloc(nullptr,0x4400000,MEM_RESERVE,PAGE_READWRITE));
    if(!base){expect(false,"publication tail fixture reserves native coordinate pages");return;}
    const bool mapped=VirtualAlloc(base+0x4197000,0x1000,MEM_COMMIT,PAGE_READWRITE)
        && VirtualAlloc(base+0x439b000,0x1000,MEM_COMMIT,PAGE_READWRITE);
    if(!mapped){expect(false,"publication tail fixture maps data pages");VirtualFree(base,0,MEM_RELEASE);return;}
    const std::uint64_t epoch=77;std::memcpy(base+0x4197170,&epoch,8);
    const int capacity=512;std::memcpy(base+0x439ba0c,&capacity,4);
    std::array<std::byte,0xa00> world{};
    auto* lock=reinterpret_cast<CRITICAL_SECTION*>(world.data()+0x270);InitializeCriticalSection(lock);
    for(unsigned failure=0;failure<10;++failure) {
        Restore p;p.base_=reinterpret_cast<std::uintptr_t>(base);p.world_=world.data();
        std::memset(world.data()+0x1d0,0,0xa0);
        const int count=38;std::memcpy(world.data()+0x1d8,&count,4);
        // The immutable allocation descriptor still has zero elements, while
        // the native sparse set contains38 publication updates. Exercise the
        // real handoff producer, not a fabricated already-correct capture.
        p.render_executing_=false;p.render_publication_work_=Restore::PublicationWork::Unavailable;
        for(unsigned i=0;i<2;++i)Restore::FingerprintRenderSet(p.render_previous_[i],p.render_previous_fingerprint_[i]);
        const auto handed=p.BeginRenderWorkExecution(4096);
        expect(handed.ok() && ReadAt<int>(p.render_publication_backing_[0].data(),8)==38
            && p.render_execution_budget_==4096 && p.owned_bytes_==4096,
            "handoff captures live queued counts rather than the empty allocation descriptor");
        expect(!p.BeginRenderWorkExecution(4096).ok() && p.owned_bytes_==4096,
            "native render ownership cannot be handed off or charged twice");
        const auto B=p.render_previous_;
        signatures=failure!=1;p.binding_valid=failure!=2;p.ownership_valid=failure!=3;
        const int native_capacity=failure==4?37:512;std::memcpy(base+0x439ba0c,&native_capacity,4);
        if(failure==5)world[0x1d0]=std::byte{1}; // Queue changed after handoff.
        if(failure==6)++p.render_previous_fingerprint_[0];
        if(failure==7)p.render_execution_settled_=true;
        p.leave_pending=failure==8;p.change_B=failure==9;
        const auto result=p.CompleteUnexecutedPublicationRenderWork();
        if(!failure) {
            expect(result.ok() && p.render_diagnostic_.observed==38 && p.native_calls==1 && p.render_publication_work_==Restore::PublicationWork::Completed
                && p.render_previous_==B && ReadAt<std::uint64_t>(base,0x4197170)==77,
                "publication tail joins native work without changing B or engine epoch");
            expect(p.CompleteUnexecutedPublicationRenderWork().ok() && p.native_calls==1,
                "completed publication tail cannot execute native work twice");
        } else {
            expect(!result.ok(),"publication tail rejects failed ownership/completion proof");
            expect(p.native_calls==(failure>=8?1u:0u),"publication admission rejects before native invocation");
            if(failure!=9)expect(p.render_previous_==B,"publication failures preserve private B image");
            if(failure>=8) {
                expect(p.render_publication_work_==Restore::PublicationWork::Completing
                    && !p.CompleteUnexecutedPublicationRenderWork().ok() && p.native_calls==1,
                    "incomplete native invocation remains retained and cannot be retried as cancelled work");
            }
        }
    }
    signatures=true;DeleteCriticalSection(lock);VirtualFree(base,0,MEM_RELEASE);
}
}
