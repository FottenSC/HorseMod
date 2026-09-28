#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

// Read-only native collection topology. Intern addresses in encounter order;
// hash ordered membership, repeated aliases, liveness and cached-parent edges.
// No expected image or reconstruction map participates in this observation.
class ReplayTraceMembership final {
public:
    struct Ref {std::uintptr_t state{},controller{};bool operator==(const Ref&)const=default;};
    bool Begin(unsigned collection,unsigned count) noexcept {
        if(collection!=collections_ || next_!=count_ || count>256 || collections_==5)return false;
        ++collections_;next_=0;count_=count;Add(collection);Add(count);return true;
    }
    bool Member(Ref ref,bool live,Ref parent) noexcept {
        if(!collections_ || next_==count_ || !ref.state || (!live && (parent.state || parent.controller)))return false;
        const auto id=Intern(ref),owner=Intern(parent);
        if(id==invalid || owner==invalid)return false;
        Add(next_++);Add(id);Add(live?1:0);Add(owner);return true;
    }
    bool complete() const noexcept {return collections_==5 && next_==count_;}
    std::uint64_t hash() const noexcept {return hash_;}
private:
    static constexpr unsigned invalid=~0u;
    unsigned Intern(Ref ref) noexcept {
        if(!ref.state && !ref.controller)return 0;
        if(!ref.controller || ref.controller>UINTPTR_MAX-16 || ref.state!=ref.controller+16)return invalid;
        for(unsigned i=0;i<size_;++i) {
            if(refs_[i]==ref)return i+1;
            if(refs_[i].state==ref.state || refs_[i].controller==ref.controller)return invalid;
        }
        if(size_==refs_.size())return invalid;
        refs_[size_++]=ref;return size_;
    }
    void Add(unsigned value) noexcept {
        for(unsigned i=0;i<4;++i)hash_=(hash_^((value>>(i*8))&255))*1099511628211ull;
    }
    // Five bounded collections and up to one parent per unique live state.
    std::array<Ref,1536> refs_{};
    unsigned size_{},collections_{},next_{},count_{};
    std::uint64_t hash_=14695981039346656037ull;
};
