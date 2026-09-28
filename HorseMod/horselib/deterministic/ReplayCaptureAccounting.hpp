#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>

namespace Horse::Deterministic {
// Intrusive reservations allocate no tracking storage. Each registered owner
// must retain all source/history objects until Retire returns. Pointer equality is
// then allocation identity, not an address from an expired historical object.
class ReplayCaptureAccounting final {
public:
    using Sources = std::array<std::uintptr_t,6>;
    struct SharedAllocation { std::uintptr_t identity{}; std::size_t bytes{}; };
    using Histories = std::array<SharedAllocation,3>;
    using Images = std::array<SharedAllocation,6>;
    struct Reservation {
        Reservation* next{};
        Sources sources{};
        Histories histories{};
        Images images{};
        std::size_t gross{}, shared{};
        bool registered{};
    };
    bool Register(Reservation& row, const Sources& sources, std::size_t gross,
        std::size_t shared, std::size_t remaining, const Histories& histories = {}, const Images& images = {}) noexcept {
        std::lock_guard lock(mutex_);
        if(row.registered || !shared || gross<shared) return false;
        for(auto source:sources) if(!source) return false;
        auto unshared=gross-shared;
        for(const auto& allocation:histories) {
            if(bool(allocation.identity)!=bool(allocation.bytes) || allocation.bytes>unshared) return false;
            unshared-=allocation.bytes;
        }
        for(const auto& allocation:images) {
            if(bool(allocation.identity)!=bool(allocation.bytes) || allocation.bytes>unshared) return false;
            unshared-=allocation.bytes;
        }
        bool found=false;
        for(auto* p=head_;p;p=p->next)
            found|=p->shared==shared && p->sources==sources;
        const auto credit=HistoryCredit(histories,nullptr)+ImageCredit(images,nullptr);
        if(gross-(found?shared:0)-credit>remaining) return false;
        row={head_,sources,histories,images,gross,shared,true};head_=&row;return true;
    }
    std::size_t bytes() const noexcept {
        std::lock_guard lock(mutex_);
        std::size_t total=0;
        for(auto* p=head_;p;p=p->next) {
            bool duplicate=false;
            for(auto* prior=head_;prior!=p;prior=prior->next)
                duplicate|=prior->shared==p->shared && prior->sources==p->sources;
            const auto charge=p->gross-(duplicate?p->shared:0)-HistoryCredit(p->histories,p)-ImageCredit(p->images,p);
            if(charge>std::numeric_limits<std::size_t>::max()-total)
                return std::numeric_limits<std::size_t>::max();
            total+=charge;
        }
        return total;
    }
    // Native releases must have succeeded first. Failed/in-flight owners stay
    // registered. Hold the accounting lock through destruction so readers
    // cannot admit allocations before the actual storage retires.
    template<class Destroy> bool Retire(Reservation& row, Destroy destroy) noexcept {
        std::lock_guard lock(mutex_);
        auto** link=&head_;
        while(*link && *link!=&row) link=&(*link)->next;
        if(!*link || !row.registered) return false;
        *link=row.next;row.registered=false;row.next=nullptr;
        destroy();return true; // destroy may delete the enclosing row.
    }
private:
    std::size_t ImageCredit(const Images& images, const Reservation* stop) const noexcept {
        std::size_t credit=0;
        for(const auto& allocation:images) {
            if(!allocation.identity) continue;
            bool found=false;
            for(auto* prior=head_;prior!=stop;prior=prior->next)
                for(const auto& retained:prior->images)
                    found|=retained.identity==allocation.identity && retained.bytes==allocation.bytes;
            if(found) credit+=allocation.bytes;
        }
        return credit;
    }
    // A retained COM owner makes pointer identity stable. Credit only the
    // identical allocation and extent, never matching descriptors alone.
    std::size_t HistoryCredit(const Histories& histories, const Reservation* stop) const noexcept {
        std::size_t credit=0;
        for(const auto& allocation:histories) {
            if(!allocation.identity) continue;
            bool found=false;
            for(auto* prior=head_;prior!=stop;prior=prior->next)
                for(const auto& retained:prior->histories)
                    found|=retained.identity==allocation.identity && retained.bytes==allocation.bytes;
            if(found) credit+=allocation.bytes;
        }
        return credit;
    }
    Reservation* head_{};
    mutable std::mutex mutex_;
};
}
