#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Horse::Deterministic {
// Reflected FBodyInstance configuration, not an image of native body ownership.
// Offsets: shipped registration1422A9300 and constructor141FE7A50. Nested
// FCollisionResponse1422AC760 owns ResponseChannel[16] (1423BE720).
class ReplayGroundBodyConfiguration {
    struct Header {std::uintptr_t data{};int count{},capacity{};};
    std::array<std::byte,0x230> values_{};
    std::array<std::array<std::byte,16>,32> responses_{};
    unsigned response_count_{};
    bool ready_{};
    template<unsigned Offset,unsigned Size,class Read>
    static bool Range(Read read,std::uintptr_t source,std::array<std::byte,0x230>& output) {
        std::array<std::byte,Size> value{};
        if(!read(source+Offset,value))return false;
        std::memcpy(output.data()+Offset,value.data(),Size);return true;
    }
public:
    // Reflected bits0..17,19..21. Bit18 is native initial-velocity state;
    // bit22 is assigned by body construction. Neither is configuration.
    static constexpr unsigned ConfigurationFlags=0x3bffff;
    bool ready()const noexcept{return ready_;}
    unsigned response_count()const noexcept{return response_count_;}
    std::uintptr_t physical_material()const noexcept {
        std::uintptr_t value{};std::memcpy(&value,values_.data()+0xd0,sizeof(value));return value;
    }
    template<class Read> bool Capture(Read read,std::uintptr_t source) noexcept {
        ReplayGroundBodyConfiguration next;
        if(!source || !Range<0x14,0x20>(read,source,next.values_)
            || !Range<0x38,8>(read,source,next.values_) || !Range<0x40,0x20>(read,source,next.values_)
            || !Range<0x74,8>(read,source,next.values_) || !Range<0x84,0x28>(read,source,next.values_)
            || !Range<0xc0,0x10>(read,source,next.values_) || !Range<0xd0,0x1c>(read,source,next.values_)
            || !Range<0x128,4>(read,source,next.values_) || !Range<0x22c,4>(read,source,next.values_))return false;
        Header responses{};
        if(!read(source+0x60,responses) || responses.count<0 || responses.count>32
            || responses.capacity<responses.count || responses.capacity>64 || (responses.capacity&&!responses.data))return false;
        next.response_count_=unsigned(responses.count);
        for(unsigned i=0;i<next.response_count_;++i)
            if(!read(responses.data+i*16,next.responses_[i]) || std::to_integer<unsigned>(next.responses_[i][8])>2)return false;
        unsigned flags{};std::memcpy(&flags,next.values_.data()+0x74,4);flags&=ConfigurationFlags;
        std::memcpy(next.values_.data()+0x74,&flags,4);
        next.ready_=true;*this=next;return true;
    }
    // Borrowed synchronous view of owned configuration. All native actors,
    // rigid IDs, callbacks, weak owners, self/user-data and shared storage stay
    // zero. A native constructor must establish those independently.
    bool BuildNativeView(std::array<std::byte,0x230>& output)const noexcept {
        if(!ready_)return false;output=values_;
        const Header h{response_count_?reinterpret_cast<std::uintptr_t>(responses_.data()):0,
            int(response_count_),int(response_count_)};
        std::memcpy(output.data()+0x60,&h,sizeof(h));return true;
    }
    template<class Read,class Write> bool ApplyCold(Read read,Write write,std::uintptr_t destination,
        std::uintptr_t response_storage,unsigned* failed_offset=nullptr,std::uintptr_t* observed=nullptr,
        std::uintptr_t* displaced_storage=nullptr)const noexcept {
        const auto reject=[&](unsigned offset,std::uintptr_t value) {
            if(failed_offset)*failed_offset=offset;if(observed)*observed=value;return false;
        };
        if(!ready_ || !destination || (response_count_&&!response_storage)
            || response_storage>UINTPTR_MAX-std::size_t(response_count_)*16
            || (displaced_storage&&*displaced_storage))return false;
        for(unsigned offset:{0xf0u,0xf8u,0x100u,0x118u,0x120u,0x210u}) {
            std::uintptr_t owner{};if(!read(destination+offset,owner) || owner)return reject(offset,owner);
        }
        Header prior{};unsigned native_flags{};
        if(!read(destination+0x60,prior))return reject(0x60,0);
        if(prior.count<0 || prior.capacity<prior.count || prior.capacity>64
            || (prior.capacity? !prior.data:bool(prior.data)))return reject(0x60,prior.data);
        if(prior.data && (!displaced_storage || *displaced_storage
            || prior.data>UINTPTR_MAX-std::size_t(prior.capacity)*16
            || (response_count_ && prior.data<response_storage+std::size_t(response_count_)*16
                && response_storage<prior.data+std::size_t(prior.capacity)*16)))return reject(0x60,prior.data);
        if(!read(destination+0x74,native_flags))return reject(0x74,0);
        // Caller owns this fresh native allocation, whose eventual destructor
        // is FBodyInstance1403AF000. Never publish the borrowed view pointer.
        for(unsigned i=0;i<response_count_;++i)
            if(!write(response_storage+i*16,responses_[i]))return false;
        const auto copy=[&]<unsigned Offset,unsigned Size>() {
            std::array<std::byte,Size> value{};
            std::memcpy(value.data(),values_.data()+Offset,Size);return write(destination+Offset,value);
        };
        unsigned configuration_flags{};std::memcpy(&configuration_flags,values_.data()+0x74,4);
        const auto flags=(native_flags&~ConfigurationFlags)|configuration_flags;
        const Header installed{response_storage,int(response_count_),int(response_count_)};
        if(!(copy.template operator()<0x14,0x20>() && copy.template operator()<0x38,8>()
            && copy.template operator()<0x40,0x20>() && write(destination+0x74,flags)
            && copy.template operator()<0x78,4>() && copy.template operator()<0x84,0x28>()
            && copy.template operator()<0xc0,0x10>() && copy.template operator()<0xd0,0x1c>()
            && copy.template operator()<0x128,4>() && copy.template operator()<0x22c,4>()))return false;
        // Retain the displaced native allocation before the header write. An
        // uncertain write leaves both allocations held by the enclosing
        // poisoned acquisition; only a completed graph may retire this pointer.
        if(displaced_storage)*displaced_storage=prior.data;
        return write(destination+0x60,installed);
    }
};
}
