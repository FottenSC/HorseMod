#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>

namespace Horse::Deterministic {
// Native 140863B30/140863660 own a 0x78 configuration, with 0x50
// placement rows and two material-parent arrays per row. This image owns
// those arrays, not any UObject. The enclosing owner must lease Assets().
// Runtime meshes, MIDs, controller state and physics are separate participants.
class ReplayGroundDebrisConfiguration {
    struct Header {std::uintptr_t data{};int count{},capacity{};};
    struct Row {std::array<std::byte,0x50> values{};std::array<std::vector<std::uintptr_t>,2> parents;};
    std::array<std::byte,0x78> values_{};
    std::vector<Row> rows_;
    std::array<std::vector<std::uintptr_t>,2> auxiliary_parents_;
    bool ready_{};
    template<class T> static T Get(const std::byte* p,unsigned offset) {
        T value{};std::memcpy(&value,p+offset,sizeof(value));return value;
    }
    template<class T> static void Put(std::byte* p,unsigned offset,const T& value) {
        std::memcpy(p+offset,&value,sizeof(value));
    }
    static bool Valid(const Header& h,unsigned limit) {
        return h.count>=0 && h.capacity>=h.count && unsigned(h.capacity)<=limit && (!h.capacity || h.data);
    }
public:
    struct NativeView {
        std::array<std::byte,0x78> configuration{};
        std::array<std::array<std::byte,0x50>,16> rows{};
        NativeView()=default;
        NativeView(const NativeView&)=delete;
        NativeView& operator=(const NativeView&)=delete;
    };
    std::size_t owned_bytes() const {
        std::size_t bytes=sizeof(*this)+rows_.capacity()*sizeof(Row);
        for(const auto& row:rows_)for(const auto& parents:row.parents)bytes+=parents.capacity()*sizeof(std::uintptr_t);
        for(const auto& parents:auxiliary_parents_)bytes+=parents.capacity()*sizeof(std::uintptr_t);
        return bytes;
    }
    template<class Read> bool Capture(Read read,std::uintptr_t source,std::size_t budget) {
        if(budget<sizeof(*this))return false;
        ReplayGroundDebrisConfiguration next;
        if(!source || !read(source,next.values_))return false;
        const auto header=Get<Header>(next.values_.data(),0);
        // Current runtime admission is ring-only. Native 1408A2B60 consumes
        // +50/+60 as material-parent lists, even when +38/+40 are null and
        // no auxiliary mesh is constructed. Preserve their authored values.
        if(!Valid(header,256) || header.count<1 || header.count>16
            || Get<std::uintptr_t>(next.values_.data(),0x38) || Get<std::uintptr_t>(next.values_.data(),0x40))return false;
        for(unsigned side=0;side<2;++side) {
            const auto offset=0x50u+side*16;
            const auto auxiliary=Get<Header>(next.values_.data(),offset);
            if(!Valid(auxiliary,32))return false;
            const auto used=next.owned_bytes();
            if(used>budget || std::size_t(auxiliary.count)>(budget-used)/sizeof(std::uintptr_t))return false;
            auto& parents=next.auxiliary_parents_[side];parents.resize(auxiliary.count);
            for(int j=0;j<auxiliary.count;++j)
                if(!read(auxiliary.data+std::size_t(j)*8,parents[j]))return false;
            Put(next.values_.data(),offset,Header{});
        }
        if(next.owned_bytes()>budget || std::size_t(header.count)>(budget-next.owned_bytes())/sizeof(Row))return false;
        next.rows_.resize(header.count);
        for(int i=0;i<header.count;++i) {
            auto& row=next.rows_[i];
            if(!read(header.data+std::size_t(i)*0x50,row.values) || !Get<std::uintptr_t>(row.values.data(),0))return false;
            for(unsigned side=0;side<2;++side) {
                const auto offset=8+side*16;
                const auto materials=Get<Header>(row.values.data(),offset);
                if(!Valid(materials,32))return false;
                const auto used=next.owned_bytes();
                if(used>budget || std::size_t(materials.count)>(budget-used)/sizeof(std::uintptr_t))return false;
                auto& parents=row.parents[side];parents.resize(materials.count);
                for(int j=0;j<materials.count;++j)
                    if(!read(materials.data+std::size_t(j)*8,parents[j]))return false;
                Put(row.values.data(),offset,Header{});
            }
        }
        Put(next.values_.data(),0,Header{});
        if(next.owned_bytes()>budget)return false;
        next.ready_=true;*this=std::move(next);return true;
    }
    // View borrows this immutable image. Never transfer its array storage to
    // native destruction; use only a verified deep-copy constructor consumer.
    bool BuildNativeView(NativeView& output) const {
        if(!ready_ || rows_.empty() || rows_.size()>output.rows.size())return false;
        output.configuration=values_;
        for(unsigned side=0;side<2;++side) {
            const auto& parents=auxiliary_parents_[side];const auto count=static_cast<int>(parents.size());
            Put(output.configuration.data(),0x50+side*16,Header{count?reinterpret_cast<std::uintptr_t>(parents.data()):0,count,count});
        }
        for(std::size_t i=0;i<rows_.size();++i) {
            output.rows[i]=rows_[i].values;
            for(unsigned side=0;side<2;++side) {
                const auto& parents=rows_[i].parents[side];
                const auto count=static_cast<int>(parents.size());
                Put(output.rows[i].data(),8+side*16,Header{count?reinterpret_cast<std::uintptr_t>(parents.data()):0,count,count});
            }
        }
        const auto count=static_cast<int>(rows_.size());
        Put(output.configuration.data(),0,Header{reinterpret_cast<std::uintptr_t>(output.rows.data()),count,count});
        return true;
    }
    template<class Visit> bool Assets(Visit visit) const {
        if(!ready_)return false;
        for(const auto& row:rows_) {
            if(!visit(Get<std::uintptr_t>(row.values.data(),0)))return false;
            for(const auto& parents:row.parents)for(auto parent:parents)if(parent && !visit(parent))return false;
        }
        for(const auto& parents:auxiliary_parents_)for(auto parent:parents)if(parent && !visit(parent))return false;
        return true;
    }
};
}
