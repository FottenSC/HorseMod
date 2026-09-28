#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <algorithm>

namespace Horse::Deterministic {
// FPhysScene +310/+330 are consumed body-transform lists, rebuilt by native
// 142029B70 after fetchResults and consumed by142028750. At a completed
// application boundary retain B's allocations privately while native C builds
// its own lists. Never restore a task, execute a stale list or copy observations.
class ReplayPhysicsPublicationLists final {
public:
    struct Array {std::uintptr_t data{};int count{},capacity{};friend bool operator==(const Array&,const Array&)=default;};
    enum class Phase {Empty,Prepared,Detached,Recovered,Committed,Poisoned};
    static constexpr unsigned maximum_capacity=8192;
    Phase phase()const noexcept{return phase_;}
    const char* failed_check()const noexcept{return failure_;}
    std::size_t owned_bytes()const noexcept {
        // C's native output backing is reserved throughout execution, not just
        // sampled at a convenient settled boundary. B stays charged through undo.
        return sizeof(*this)+(phase_==Phase::Empty?0:2*maximum_capacity*sizeof(std::uintptr_t))
            +std::size_t(original_[0].capacity+original_[1].capacity)*sizeof(std::uintptr_t);
    }
    template<class Read> bool Prepare(std::uintptr_t owner,std::span<const std::uintptr_t> bodies,Read read) noexcept {
        if(phase_!=Phase::Empty || !owner)return false;
        std::array<Array,2> arrays{};std::array<std::array<std::uintptr_t,32>,2> entries{};
        failure_="publication_B_membership";
        for(unsigned i=0;i<2;++i) {
            if(!read(owner+0x310+i*0x20,arrays[i]) || !Valid(arrays[i]) || arrays[i].count>32)return false;
            for(int j=0;j<arrays[i].count;++j) {
                auto& value=entries[i][j];
                if(!read(arrays[i].data+j*8,value))return false;
                if(value && std::find(bodies.begin(),bodies.end(),value)==bodies.end())return false;
                for(unsigned prior=0;value && prior<=i;++prior)
                    for(unsigned k=0;k<(prior==i?unsigned(j):unsigned(arrays[prior].count));++k)
                        if(entries[prior][k]==value)return false;
            }
        }
        if(Overlap(arrays[0],arrays[1]))return false;
        owner_=owner;original_=arrays;entries_=entries;phase_=Phase::Prepared;failure_="none";return true;
    }
    template<class Read,class Write> bool Detach(Read read,Write write) noexcept {
        if(phase_==Phase::Detached)return true;
        if(phase_!=Phase::Prepared || !Original(read,true))return false;
        for(unsigned i=0;i<2;++i)if(!write(owner_+0x310+i*0x20,Array{}))return Poison("publication_detach");
        phase_=Phase::Detached;return true;
    }
    template<class Read,class Write,class Free> bool Recover(Read read,Write write,Free release) noexcept {
        if(phase_==Phase::Empty || phase_==Phase::Recovered)return true;
        if(phase_==Phase::Prepared) {
            if(!Original(read,true))return false;
            phase_=Phase::Recovered;return true;
        }
        if(phase_!=Phase::Detached || !Original(read,false))return false;
        std::array<Array,2> current{};
        failure_="publication_C_storage";
        for(unsigned i=0;i<2;++i) {
            if(!read(owner_+0x310+i*0x20,current[i]) || !Valid(current[i]))return false;
            for(const auto& original:original_)if(Overlap(current[i],original))return false;
        }
        if(Overlap(current[0],current[1]))return false;
        retiring_=current; // Keep displaced pointers even if a native call faults.
        // The enclosing engine/application/GPU owner has completed C. These
        // TArray<FBodyInstance*> buffers own no references or element destructors.
        // Publish B before releasing displaced C; uncertain writes/free poison
        // the operation and retain every remaining owner instead of retrying.
        for(unsigned i=0;i<2;++i) {
            if(!write(owner_+0x310+i*0x20,original_[i]))return Poison("publication_undo_write");
            if(current[i].data && !release(current[i].data))return Poison("publication_C_retirement");
            retiring_[i]={};
        }
        phase_=Phase::Recovered;failure_="none";return true;
    }
    // Irreversible enclosing commit only. C keeps its current output lists;
    // these private B allocations contain pointers, not owning references.
    template<class Read> bool CanCommit(Read read) noexcept {
        if(phase_==Phase::Empty || phase_==Phase::Committed)return true;
        if(phase_!=Phase::Detached || !Original(read,false))return false;
        std::array<Array,2> current{};
        failure_="publication_commit_C_storage";
        for(unsigned i=0;i<2;++i) {
            if(!read(owner_+0x310+i*0x20,current[i]) || !Valid(current[i]))return false;
            for(const auto& original:original_)if(Overlap(current[i],original))return false;
        }
        return !Overlap(current[0],current[1]);
    }
    template<class Read,class Free> bool Commit(Read read,Free release) noexcept {
        if(phase_==Phase::Empty || phase_==Phase::Committed)return true;
        if(!CanCommit(read))return false;
        for(auto& original:original_) {
            if(original.data && !release(original.data))return Poison("publication_B_retirement");
            original={};
        }
        phase_=Phase::Committed;failure_="none";return true;
    }
private:
    static bool Valid(const Array& a) noexcept {
        return a.count>=0 && a.capacity>=a.count && a.capacity<=int(maximum_capacity)
            && (a.capacity?bool(a.data):!a.data)
            && a.data<=UINTPTR_MAX-std::size_t(a.capacity)*8;
    }
    static bool Overlap(const Array& a,const Array& b) noexcept {
        return a.capacity && b.capacity && a.data<b.data+std::size_t(b.capacity)*8 && b.data<a.data+std::size_t(a.capacity)*8;
    }
    template<class Read> bool Original(Read read,bool published) noexcept {
        failure_="publication_B_changed";
        for(unsigned i=0;i<2;++i) {
            Array current{};
            if(published && (!read(owner_+0x310+i*0x20,current) || current!=original_[i]))return false;
            for(int j=0;j<original_[i].count;++j) {
                std::uintptr_t value{};
                if(!read(original_[i].data+j*8,value) || value!=entries_[i][j])return false;
            }
        }
        return true;
    }
    bool Poison(const char* why)noexcept{failure_=why;phase_=Phase::Poisoned;return false;}
    std::uintptr_t owner_{};std::array<Array,2> original_{};
    std::array<Array,2> retiring_{};
    std::array<std::array<std::uintptr_t,32>,2> entries_{};
    Phase phase_{Phase::Empty};const char* failure_{"none"};
};
}
