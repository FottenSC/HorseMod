#include "Sc6ReplaySchedulerState.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <algorithm>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
template<class T, std::size_t N> T Field(const std::array<std::byte, N>& bytes, std::size_t offset)
{ T result; std::memcpy(&result, bytes.data() + offset, sizeof(result)); return result; }
template<class T, std::size_t N> void Store(std::array<std::byte, N>& bytes, std::size_t offset, const T& value)
{ std::memcpy(bytes.data() + offset, &value, sizeof(value)); }
bool SameBytes(std::uintptr_t address, const void* expected, std::size_t size) noexcept
{
    if (!size) return true;
    if (!address || size > UINTPTR_MAX - address) return false;
    __try { return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WriteBytes(std::uintptr_t address, const void* data, std::size_t size) noexcept
{
    __try { std::memcpy(reinterpret_cast<void*>(address), data, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Writable(std::uintptr_t address, std::size_t size) noexcept
{
    if (!address || size > UINTPTR_MAX - address) return false;
    const auto end = address + size;
    while (address < end)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))
            || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = region.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY
            && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}
bool Overlap(std::uintptr_t a, std::size_t as, std::uintptr_t b, std::size_t bs) noexcept
{
    return as && bs && (as > UINTPTR_MAX - a || bs > UINTPTR_MAX - b || (a < b + bs && b < a + as));
}
bool WeakIsLive(std::uintptr_t base, const void* weak) noexcept
{
    __try { return reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(weak) != nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

Sc6ReplaySchedulerState::PreparedRestore::~PreparedRestore() { Clear(); }
Status Sc6ReplaySchedulerState::RestoreStageRegistration(const PreparedRestore& prepared,
    const ReplayStageVisibility::Component& component,bool publish) const noexcept
{
    // Only complete, independently verified B scheduler installation can
    // authorize restoring its component-level registration flag. Replaying
    // RegisterAllComponentTickFunctions here could enable a tick or append a
    // prerequisite after exact B installation; restore its already-owned
    // result instead. All tasks remain quiescent under the enclosing owner.
    if(!OwnsStageComponent(component) || prepared.previous_image_!=this
        || !prepared.ready_ || prepared.published_ || !prepared.executing_
        || !prepared.execution_settled_ || !prepared.execution_undo_started_
        || prepared.thread_!=GetCurrentThreadId() || prepared.base_!=base_
        || !SameBytes(base_+0x4197170,&prepared.epoch_,8))
        return Status::failure(FailureCode::UndoFailed);
    std::uint64_t fingerprint{};
    if(!prepared.PreviousFingerprint(fingerprint) || fingerprint!=prepared.previous_fingerprint_
        || !ValidatePrivateBacking().ok() || !ValidateBindings(base_,prepared.world_).ok())
        return Status::failure(FailureCode::UndoFailed);
    for(const auto& patch:prepared.patches_)
        if(!SameBytes(patch.destination,patch.previous.data(),patch.size))return Status::failure(FailureCode::UndoFailed);
    __try {
        const auto address=component.object+0x188;
        auto flags=*reinterpret_cast<const std::uint32_t*>(address);
        if(!(flags&1) || !Writable(address,4))return Status::failure(FailureCode::UndoFailed);
        if(publish) {
            flags=(flags&~0x20000000u)|(component.flags&0x20000000u);
            if(!WriteBytes(address,&flags,4) || !SameBytes(address,&flags,4))return Status::failure(FailureCode::UndoFailed);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::UndoFailed);}
    return Status::success();
}
void Sc6ReplaySchedulerState::PreparedRestore::Clear() noexcept
{
    // Failure/abandonment cannot free replacement arrays still reachable from
    // native scheduler headers. Explicit undo or commit must retire this owner.
    if (published_ || allocation_ownership_lost_) return;
    for (auto& allocation : allocations_) {
        if (allocation.data) reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(allocation.data);
        // Before execution, undo puts this buffer back on the fresh native
        // tick. After execution, recovery retired that owner and restored only
        // B ticks: its detached constructor allocation has no native consumer.
        // The same enclosing completed-work boundary already permits C data
        // reclamation above. Published/poisoned operations remain retained.
        if(executing_ && execution_settled_ && execution_undo_started_
            && allocation.previous_is_added && allocation.previous)
            reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(allocation.previous);
    }
    std::vector<Allocation>().swap(allocations_);
    std::vector<Patch>().swap(patches_);
    base_ = 0;
    world_ = nullptr;
    epoch_ = target_epoch_ = 0;
    ready_ = false;
    birth_ = {};
    excluded_ticks_ = {};
    excluded_owners_ = {}; private_owners_ = {};
    excluded_count_ = 0;
    previous_image_ = nullptr;
    added_ticks_ = {}; added_count_ = 0;
    published_fingerprint_ = 0;
    execution_image_ = nullptr; original_epoch_ = previous_fingerprint_ = 0;
    execution_reservation_ = 0;
    executing_ = execution_settled_ = execution_undo_started_ = false;
}
std::size_t Sc6ReplaySchedulerState::PreparedRestore::owned_bytes() const noexcept
{
    auto bytes = sizeof(*this) + allocations_.capacity() * sizeof(Allocation) + patches_.capacity() * sizeof(Patch) + execution_reservation_;
    for (const auto& allocation : allocations_) bytes += allocation.charged;
    return bytes;
}
std::size_t Sc6ReplaySchedulerState::PreparedRestore::allocation_count() const noexcept
{
    return std::count_if(allocations_.begin(), allocations_.end(), [](const Allocation& allocation) { return allocation.data != nullptr; });
}

Status Sc6ReplaySchedulerState::PreparedRestore::Allocate(std::size_t requested, const void* source,
    std::size_t copied, std::uintptr_t previous, std::size_t previous_requested, std::size_t budget, std::uintptr_t& address)
{
    address = 0;
    if (copied > requested || (copied && !source) || bool(previous) != bool(previous_requested))
        return Status::failure(FailureCode::RestorePreflightFailed);
    if (!requested && !previous_requested) return Status::success();
    const auto quantize = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base_ + 0xd50dc0);
    const auto charged = requested ? quantize(requested, 0) : 0;
    const auto previous_charged = previous_requested ? quantize(previous_requested, 0) : 0;
    const auto used = owned_bytes();
    if (charged < requested || previous_charged < previous_requested || used > budget || charged > budget - used)
        return Status::failure(FailureCode::CapacityExceeded);
    // Metadata was reserved before native allocations. Register ownership
    // before copying so every later failure releases the new allocation.
    allocations_.emplace_back();
    auto& allocation = allocations_.back();
    allocation.previous = reinterpret_cast<void*>(previous);
    allocation.previous_requested = previous_requested;
    allocation.previous_charged = previous_charged;
    // An old heap can be replaced by inline/empty storage. Keep its ownership
    // record for commit even when no replacement heap allocation is needed.
    if (!requested) return Status::success();
    allocation.data = reinterpret_cast<void* (*)(std::size_t)>(base_ + 0x4a61c0)(requested);
    if (!allocation.data) return Status::failure(FailureCode::CapacityExceeded);
    allocation.requested = requested;
    allocation.copied = copied;
    allocation.charged = charged;
    std::memset(allocation.data, 0, requested);
    if (copied) std::memcpy(allocation.data, source, copied);
    address = reinterpret_cast<std::uintptr_t>(allocation.data);
    return Status::success();
}

Status Sc6ReplaySchedulerState::PreparedRestore::ValidateAddedTicks() const noexcept
{
    for (std::size_t i = 0; i < added_count_; ++i) {
        const auto& tick = added_ticks_[i];
        if (!tick.lease) return Status::failure(FailureCode::GenerationMismatch);
        const auto status = tick.lease->ValidateObject(reinterpret_cast<void*>(tick.owner));
        if (!status.ok()) return status;
        if (!SameBytes(tick.address, tick.previous.data(), tick.previous.size()))
            return Status::failure(FailureCode::GenerationMismatch);
        const auto count=Field<int>(tick.previous,0x28),capacity=Field<int>(tick.previous,0x2c);
        if(count<0 || count>1 || capacity<count || capacity>16
            || !SameBytes(Field<std::uintptr_t>(tick.previous,0x20),tick.constructor_prerequisites.data(),std::size_t(capacity)*16)
            || (count && !WeakIsLive(base_,tick.constructor_prerequisites.data())))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    return Status::success();
}

Status Sc6ReplaySchedulerState::PrepareRestore(const Sc6ReplaySchedulerState& current, std::uintptr_t base, void* world,
    std::size_t budget, PreparedRestore& output, PreflightFailure* failure,
    const Sc6ReplayVfxState::ParticleBirthSet* births, RetainedPrimaryTicks private_owners,
    std::span<const Sc6ReplayVfxState::ReconstructionBinding> fresh_components) const noexcept
{
    const auto checking = [&](const char* name, std::uintptr_t address = 0) {
        if (failure) *failure = {name, address};
    };
    checking("prepared_storage_empty");
    if (output.ready_ || !output.allocations_.empty() || !output.patches_.empty())
        return Status::failure(FailureCode::IllegalTransition);
    checking("target_bindings");
    auto status = ValidateBindings(base, world);
    if (!status.ok()) return status;
    checking("current_image");
    status = current.ValidateCurrentImage(base, world);
    if (!status.ok()) return status;
    checking("membership_counts");
    std::array<const Tick*, PreparedRestore::excluded_capacity> excluded{};
    std::array<std::uintptr_t, PreparedRestore::excluded_capacity> owners{};
    std::size_t excluded_count = births ? births->members().size() : 0;
    if(excluded_count>owners.size())return Status::failure(FailureCode::GenerationMismatch);
    for(std::size_t i=0;i<excluded_count;++i)owners[i]=births->members()[i].component();
    if(private_owners.visit) {
        struct Collect {decltype(owners)& owners;std::size_t& count;} collect{owners,excluded_count};
        checking("private_tick_owners");
        status=private_owners.visit(private_owners.context,&collect,[](void* context,std::uintptr_t owner) {
            auto& c=*static_cast<Collect*>(context);
            if(!owner || c.count==c.owners.size() || std::find(c.owners.begin(),c.owners.begin()+c.count,owner)!=c.owners.begin()+c.count)return false;
            c.owners[c.count++]=owner;return true;
        });
        if(!status.ok())return status;
    } else if(private_owners.context)return Status::failure(FailureCode::GenerationMismatch);
    if (births) {
        checking("particle_birth_owners");
        status = births->ValidateCurrentB();
        if (!status.ok()) return status;
    }
    // Fresh A-only ticks have a native, unregistered constructor prefix. They
    // belong to the operation, never to previous_image_ / complete B.
    std::array<PreparedRestore::AddedTick, PreparedRestore::excluded_capacity> added{};
    std::size_t added_count{};
    for (const auto& binding : fresh_components) {
        const auto& identity = binding.identity;
        if (identity.source == identity.target && identity.source_weak == identity.target_weak) continue;
        checking("fresh_tick_owner", identity.target);
        if (!identity.source || !identity.target || !binding.lease || added_count == added.size())
            return Status::failure(FailureCode::GenerationMismatch);
        status = binding.lease->ValidateObject(reinterpret_cast<void*>(identity.target));
        if (!status.ok()) return status;
        for (std::size_t i = 0; i < added_count; ++i) if (added[i].owner == identity.target)
            return Status::failure(FailureCode::GenerationMismatch);
        const Tick* target{};
        for (const auto& tick : ticks_) if (tick.owner.object == identity.target) {
            if (target || tick.owner.weak != identity.target_weak || tick.address - identity.target != 0x110)
                return Status::failure(FailureCode::UnsupportedContent);
            target = &tick;
        }
        if (!target || std::any_of(current.ticks_.begin(), current.ticks_.end(),
            [&](const auto& tick) {return tick.owner.object == identity.target || tick.address == target->address;}))
            return Status::failure(FailureCode::GenerationMismatch);
        auto& fresh = added[added_count++]; fresh.address = target->address; fresh.owner = identity.target; fresh.lease = binding.lease;
        if (!WriteBytes(reinterpret_cast<std::uintptr_t>(fresh.previous.data()), reinterpret_cast<const void*>(fresh.address), fresh.previous.size()))
            return Status::failure(FailureCode::ContextUnavailable);
        // Native registration was removed under the private preparation hold.
        // A trace constructor may own one common prerequisite. Journal all of
        // its backing; Allocate below retains this exact previous allocation
        // for undo and retires it only at the enclosing irreversible commit.
        if ((Field<std::uint8_t>(fresh.previous, 0xc) & 0x40)
            || Field<std::uintptr_t>(fresh.previous, 0x18)
            || Field<std::uintptr_t>(fresh.previous, 0x48) != Field<std::uintptr_t>(target->binding_layout, 0x48)
            || Field<std::uintptr_t>(fresh.previous, 0x50) != identity.target)
            return Status::failure(FailureCode::UnsupportedContent);
        const auto count=Field<int>(fresh.previous,0x28),capacity=Field<int>(fresh.previous,0x2c);
        const auto pointer=Field<std::uintptr_t>(fresh.previous,0x20);
        if(count<0 || count>1 || capacity<count || capacity>16 || bool(pointer)!=bool(capacity)
            || (!count && capacity) || std::size_t(capacity)*16>UINTPTR_MAX-pointer)
            return Status::failure(FailureCode::UnsupportedContent);
        if(count) {
            if(!WriteBytes(reinterpret_cast<std::uintptr_t>(fresh.constructor_prerequisites.data()),
                reinterpret_cast<const void*>(pointer),std::size_t(capacity)*16)
                || !WeakIsLive(base,fresh.constructor_prerequisites.data()))
                return Status::failure(FailureCode::GenerationMismatch);
            const auto prerequisite=Field<std::uintptr_t>(fresh.constructor_prerequisites,8);
            const auto matches=[&](const Tick& tick) {
                return tick.address==prerequisite
                    && !std::memcmp(tick.owner.weak.data(),fresh.constructor_prerequisites.data(),8);
            };
            const auto in_a=std::find_if(ticks_.begin(),ticks_.end(),matches);
            const auto in_b=std::find_if(current.ticks_.begin(),current.ticks_.end(),matches);
            if(in_a==ticks_.end() || in_b==current.ticks_.end() || in_a->owner.object!=in_b->owner.object)
                return Status::failure(FailureCode::GenerationMismatch);
        }
    }
    checking("membership_counts");
    if (levels_.size() != current.levels_.size() || current.ticks_.size() + added_count != ticks_.size() + excluded_count) {
        if (failure) {
            failure->target_ticks=ticks_.size(); failure->current_ticks=current.ticks_.size();
            failure->births=excluded_count;
            const auto differences=[&](const auto& first,const auto& second,bool is_current) {
                for (const auto& tick:first) {
                    if (std::any_of(second.begin(),second.end(),[&](const auto& other){return other.address==tick.address;})) continue;
                    if (failure->difference_count<failure->differences.size())
                        failure->differences[failure->difference_count++]={tick.address,tick.owner.object,is_current};
                }
            };
            differences(ticks_,current.ticks_,false);
            differences(current.ticks_,ticks_,true);
        }
        return Status::failure(FailureCode::GenerationMismatch);
    }
    for (std::size_t i = 0; i < excluded_count; ++i) {
        const auto owner = owners[i];
        checking("excluded_tick_membership", owner);
        for (const auto& item : current.ticks_) {
            if (item.owner.object != owner) continue;
            if (excluded[i] || !(Field<std::uint8_t>(item.binding_layout, 0xc) & 0x40))
                return Status::failure(FailureCode::GenerationMismatch);
            excluded[i] = &item;
        }
        if (!excluded[i] || std::any_of(ticks_.begin(), ticks_.end(), [&](const Tick& item) {
                return item.owner.object == owner || item.address == excluded[i]->address;
            })) return Status::failure(FailureCode::GenerationMismatch);
        for (std::size_t j = 0; j < i; ++j) if (excluded[i] == excluded[j])
            return Status::failure(FailureCode::GenerationMismatch);
    }
    // Count equality alone cannot hide an unrelated added/replaced tick.
    for (const auto& item : current.ticks_) {
        if (std::any_of(ticks_.begin(), ticks_.end(), [&](const Tick& a) { return a.address == item.address; })) continue;
        if (std::none_of(excluded.begin(), excluded.begin()+excluded_count, [&](const Tick* row) { return row == &item; })) {
            checking("new_tick_registration", item.address);
            return Status::failure(FailureCode::GenerationMismatch);
        }
    }
    checking("storage_budget");
    const auto target_bytes = owned_bytes();
    const auto current_bytes = this == &current ? 0 : current.owned_bytes();
    if (target_bytes > budget || current_bytes > budget - target_bytes)
        return Status::failure(FailureCode::CapacityExceeded);
    const auto remaining = budget - target_bytes - current_bytes;
    // Three sets and a cooldown head per level, then one common tick prefix.
    // Each set can own sparse slots, heap flags and heap hash buckets.
    const auto patch_count = levels_.size() * 4 + ticks_.size() + excluded_count;
    const auto allocation_count = levels_.size() * 9 + ticks_.size();
    if (patch_count > remaining / sizeof(PreparedRestore::Patch)
        || allocation_count > (remaining - patch_count * sizeof(PreparedRestore::Patch)) / sizeof(PreparedRestore::Allocation))
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        output.base_ = base;
        output.world_ = world;
        output.epoch_ = current.epoch_;
        output.original_epoch_ = current.epoch_;
        output.target_epoch_ = epoch_;
        output.thread_ = GetCurrentThreadId();
        if (births) output.birth_ = *births;
        output.excluded_count_ = excluded_count;
        output.excluded_owners_=owners; output.private_owners_=private_owners;
        for (std::size_t i = 0; i < excluded_count; ++i) output.excluded_ticks_[i] = excluded[i]->address;
        output.previous_image_ = &current;
        output.added_ticks_ = added; output.added_count_ = added_count;
        output.patches_.reserve(patch_count);
        output.allocations_.reserve(allocation_count);
        if (output.owned_bytes() > remaining) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        const auto allocate = [&](auto& patch, std::size_t offset, std::size_t requested, std::size_t previous_requested,
            const void* data, std::size_t copied,bool previous_added=false) {
            const auto previous = Field<std::uintptr_t>(patch.previous, offset);
            std::uintptr_t address{};
            const auto result = output.Allocate(requested, data, copied, previous, previous_requested, remaining, address);
            if(result.ok() && previous_requested && previous_added)output.allocations_.back().previous_is_added=true;
            if (result.ok()) Store(patch.bytes, offset, address);
            return result;
        };
        for (const auto& level : levels_)
        {
            checking("level_binding", level.address);
            const auto live = std::find_if(current.levels_.begin(), current.levels_.end(),
                [&](const Level& item) { return item.address == level.address; });
            if (live == current.levels_.end() || live->owner.object != level.owner.object || live->owner.weak != level.owner.weak)
            { output.Clear(); return Status::failure(FailureCode::GenerationMismatch); }
            constexpr std::array<std::size_t, 3> offsets{8, 0x60, 0xc0};
            for (std::size_t i = 0; i < offsets.size(); ++i)
            {
                const auto& set = level.sets[i];
                output.patches_.emplace_back();
                auto& patch = output.patches_.back();
                patch.destination = level.address + offsets[i];
                patch.size = set.binding_layout.size();
                std::memcpy(patch.bytes.data(), set.binding_layout.data(), patch.size);
                std::memcpy(patch.previous.data(), live->sets[i].binding_layout.data(), patch.size);
                const auto capacity = Field<int>(set.binding_layout, 0xc);
                const auto max_bits = Field<int>(set.binding_layout, 0x2c);
                const auto hash_size = Field<int>(set.binding_layout, 0x48);
                if (capacity < 0 || max_bits < 0 || hash_size < 0)
                { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
                status = allocate(patch, 0, static_cast<std::size_t>(capacity) * 16,
                    static_cast<std::size_t>(Field<int>(patch.previous, 0xc)) * 16,
                    set.slots.data(), set.slots.size());
                const bool heap_flags = Field<std::uintptr_t>(set.binding_layout, 0x20) != 0;
                const bool old_heap_flags = Field<std::uintptr_t>(patch.previous, 0x20) != 0;
                if (status.ok() && (heap_flags || old_heap_flags))
                    status = allocate(patch, 0x20, heap_flags ? ((static_cast<std::size_t>(max_bits) + 31) / 32) * 4 : 0,
                        old_heap_flags ? ((static_cast<std::size_t>(Field<int>(patch.previous, 0x2c)) + 31) / 32) * 4 : 0,
                        set.allocation_flags.data(), heap_flags ? set.allocation_flags.size() * 4 : 0);
                const bool heap_hash = Field<std::uintptr_t>(set.binding_layout, 0x40) != 0;
                const bool old_heap_hash = Field<std::uintptr_t>(patch.previous, 0x40) != 0;
                if (status.ok() && (heap_hash || old_heap_hash))
                    status = allocate(patch, 0x40, heap_hash ? static_cast<std::size_t>(hash_size) * 4 : 0,
                        old_heap_hash ? static_cast<std::size_t>(Field<int>(patch.previous, 0x48)) * 4 : 0,
                        set.hash.data(), heap_hash ? set.hash.size() * 4 : 0);
                if (!status.ok()) { output.Clear(); return status; }
            }
            output.patches_.emplace_back();
            auto& head = output.patches_.back();
            head.destination = level.address + 0x58;
            head.size = sizeof(std::uintptr_t);
            Store(head.bytes, 0, level.cooldown_order.empty() ? std::uintptr_t{} : level.cooldown_order.front());
            Store(head.previous, 0, live->cooldown_order.empty() ? std::uintptr_t{} : live->cooldown_order.front());
        }
        for (const auto& tick : ticks_)
        {
            checking("tick_binding", tick.address);
            const auto live = std::find_if(current.ticks_.begin(), current.ticks_.end(),
                [&](const Tick& item) { return item.address == tick.address; });
            const PreparedRestore::AddedTick* fresh{};
            if (live == current.ticks_.end()) {
                for (std::size_t i = 0; i < added_count; ++i) if (added[i].address == tick.address) {fresh = &added[i]; break;}
                if (!fresh || fresh->owner != tick.owner.object)
                { output.Clear(); return Status::failure(FailureCode::GenerationMismatch); }
            } else if (live->owner.object != tick.owner.object || live->owner.weak != tick.owner.weak)
            { output.Clear(); return Status::failure(FailureCode::GenerationMismatch); }
            const auto& previous = fresh ? fresh->previous : live->binding_layout;
            if (epoch_ != current.epoch_)
                for (const auto& prerequisite : tick.prerequisites)
                {
                    const auto address = Field<std::uintptr_t>(prerequisite, 8);
                    checking("prerequisite_live", tick.address);
                    if (!WeakIsLive(base, prerequisite.data()))
                    { output.Clear(); return Status::failure(FailureCode::GenerationMismatch); }
                    checking("prerequisite_membership", address);
                    if (std::none_of(ticks_.begin(), ticks_.end(), [&](const Tick& item) { return item.address == address; }))
                    { output.Clear(); return Status::failure(FailureCode::UnsupportedContent); }
                }
            // No inferred historical semantics for these unclassified bytes.
            checking("unclassified_tick_bytes", tick.address);
            if (std::memcmp(tick.binding_layout.data() + 0xe, previous.data() + 0xe, 2)
                || std::memcmp(tick.binding_layout.data() + 0x44, previous.data() + 0x44, 4))
            { output.Clear(); return Status::failure(FailureCode::UnsupportedContent); }
            output.patches_.emplace_back();
            auto& patch = output.patches_.back();
            patch.destination = tick.address;
            patch.bytes = tick.binding_layout;
            patch.previous = previous;
            patch.size = tick.binding_layout.size();
            const auto capacity = Field<int>(tick.binding_layout, 0x2c);
            checking("prerequisite_allocation", tick.address);
            if (capacity < 0) { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            status = allocate(patch, 0x20, static_cast<std::size_t>(capacity) * 16,
                static_cast<std::size_t>(Field<int>(patch.previous, 0x2c)) * 16,
                tick.prerequisites.data(), tick.prerequisites.size() * 16,fresh!=nullptr);
            if (!status.ok()) { output.Clear(); return status; }
            if (epoch_ != current.epoch_)
                for (const auto offset : {0x10u, 0x14u})
                {
                    checking("epoch_stamp_rebase", tick.address + offset);
                    std::int32_t stamp{};
                    if (!RebaseEpochStamp(Field<std::int32_t>(tick.binding_layout, offset), epoch_, current.epoch_, stamp))
                    { output.Clear(); return Status::failure(FailureCode::UnsupportedContent); }
                    Store(patch.bytes, offset, stamp);
                }
        }
        for (std::size_t i = 0; i < excluded_count; ++i)
        {
            const auto* removed = excluded[i];
            // Native 142157BC0 removes level membership, then clears +C bit
            // 0x40. A's set/cooldown patches perform the membership part. Keep
            // the born tick's vtable, level binding and prerequisites for B undo;
            // do not call its destructor or reclaim its backing.
            output.patches_.emplace_back();
            auto& patch = output.patches_.back();
            patch.destination = removed->address + 0xc;
            patch.size = 1;
            const auto flags = Field<std::uint8_t>(removed->binding_layout, 0xc);
            Store(patch.previous, 0, flags);
            Store(patch.bytes, 0, static_cast<std::uint8_t>(flags & ~0x40u));
        }
        // Preparation never invokes level/tick destructors: 142157C10 and
        // 142157BC0 also change registration flags on live tick objects.
        // Native task pointers remain null. Epoch comparisons are rebased,
        // never the global counter. Other world components remain the enclosing
        // simulation transaction's responsibility before any resumed execution.
        status = output.ValidateAddedTicks();
        if (!status.ok()) { output.Clear(); return status; }
        output.ready_ = true;
        if (failure) *failure = {};
        return Status::success();
    }
    catch (...) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplaySchedulerState::ValidateCurrentImage(std::uintptr_t base, void* world) const noexcept
{
    const auto status = ValidateBindings(base, world);
    if (!status.ok()) return status;
    if (!SameBytes(base + 0x4197170, &epoch_, sizeof(epoch_)))
        return Status::failure(FailureCode::GenerationMismatch);
    for (const auto& level : levels_)
    {
        const auto head = level.cooldown_order.empty() ? std::uintptr_t{} : level.cooldown_order.front();
        if (!SameBytes(level.address + 0x58, &head, sizeof(head)))
            return Status::failure(FailureCode::GenerationMismatch);
        constexpr std::array<std::size_t, 3> offsets{8, 0x60, 0xc0};
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            const auto& set = level.sets[i];
            const auto address = level.address + offsets[i];
            const auto flags = Field<std::uintptr_t>(set.binding_layout, 0x20);
            const auto hash = Field<std::uintptr_t>(set.binding_layout, 0x40);
            if (!SameBytes(address, set.binding_layout.data(), set.binding_layout.size())
                || !SameBytes(Field<std::uintptr_t>(set.binding_layout, 0), set.slots.data(), set.slots.size())
                || !SameBytes(flags ? flags : address + 0x10, set.allocation_flags.data(), set.allocation_flags.size() * 4)
                || !SameBytes(hash ? hash : address + 0x38, set.hash.data(), set.hash.size() * 4))
                return Status::failure(FailureCode::GenerationMismatch);
        }
    }
    for (const auto& tick : ticks_)
        if (!SameBytes(tick.address, tick.binding_layout.data(), tick.binding_layout.size())
            || !SameBytes(Field<std::uintptr_t>(tick.binding_layout, 0x20), tick.prerequisites.data(), tick.prerequisites.size() * 16))
            return Status::failure(FailureCode::GenerationMismatch);
    return Status::success();
}

Status Sc6ReplaySchedulerState::PublishBacking(const Sc6ReplaySchedulerState& current,
    std::uintptr_t base, void* world, std::size_t budget, PreparedRestore& prepared) const noexcept
{
    if (!prepared.ready_ || prepared.published_ || prepared.thread_ != GetCurrentThreadId())
        return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateBindings(base, world);
    if (!status.ok()) return status;
    status = current.ValidateCurrentImage(base, world);
    if (status.ok()) status = prepared.ValidateAddedTicks();
    if (!status.ok()) return status;
    if (prepared.base_ != base || prepared.world_ != world || prepared.epoch_ != current.epoch_
        || prepared.target_epoch_ != epoch_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (prepared.excluded_count_)
    {
        status = prepared.birth_.ValidateCurrentB();
        if(status.ok())status=prepared.ValidatePrivateOwners();
        if (!status.ok()) return status;
    }
    // Both captured images, prepared buffers and old live backing retained
    // through commit share this allowance. The enclosing replay owns its budget.
    auto remaining = budget;
    const auto charge = [&remaining](std::size_t bytes) {
        if (bytes > remaining) return false;
        remaining -= bytes;
        return true;
    };
    if (!charge(owned_bytes()) || (this != &current && !charge(current.owned_bytes())) || !charge(prepared.owned_bytes()))
        return Status::failure(FailureCode::CapacityExceeded);
    for (const auto& allocation : prepared.allocations_)
        if (!charge(allocation.previous_charged)) return Status::failure(FailureCode::CapacityExceeded);

    // Verify prepared payloads against this target, independently of current
    // native payloads. A historical target is expected to contain different data.
    if (prepared.patches_.size() != levels_.size() * 4 + ticks_.size() + prepared.excluded_count_)
        return Status::failure(FailureCode::RestorePreflightFailed);
    const auto payload_matches = [&](std::uintptr_t address, std::size_t requested, const void* source, std::size_t copied) {
        if (!requested) return !address && !copied;
        const auto allocation = std::find_if(prepared.allocations_.begin(), prepared.allocations_.end(),
            [&](const PreparedRestore::Allocation& item) { return reinterpret_cast<std::uintptr_t>(item.data) == address; });
        return allocation != prepared.allocations_.end() && allocation->requested == requested
            && allocation->copied == copied && SameBytes(address, source, copied);
    };
    std::size_t patch_index = 0;
    for (const auto& level : levels_)
    {
        constexpr std::array<std::size_t, 3> offsets{8, 0x60, 0xc0};
        for (std::size_t i = 0; i < offsets.size(); ++i)
        {
            const auto& set = level.sets[i];
            const auto& patch = prepared.patches_[patch_index++];
            std::array<std::byte, 0x58> expected{};
            std::memcpy(expected.data(), set.binding_layout.data(), set.binding_layout.size());
            const auto slots = Field<std::uintptr_t>(patch.bytes, 0);
            const auto flags = Field<std::uintptr_t>(patch.bytes, 0x20);
            const auto hash = Field<std::uintptr_t>(patch.bytes, 0x40);
            if (!payload_matches(slots, static_cast<std::size_t>(Field<int>(expected, 0xc)) * 16,
                set.slots.data(), set.slots.size())) return Status::failure(FailureCode::RestorePreflightFailed);
            Store(expected, 0, slots);
            if (Field<std::uintptr_t>(expected, 0x20))
            {
                if (!payload_matches(flags, ((static_cast<std::size_t>(Field<int>(expected, 0x2c)) + 31) / 32) * 4,
                    set.allocation_flags.data(), set.allocation_flags.size() * 4)) return Status::failure(FailureCode::RestorePreflightFailed);
                Store(expected, 0x20, flags);
            }
            if (Field<std::uintptr_t>(expected, 0x40))
            {
                if (!payload_matches(hash, static_cast<std::size_t>(Field<int>(expected, 0x48)) * 4,
                    set.hash.data(), set.hash.size() * 4)) return Status::failure(FailureCode::RestorePreflightFailed);
                Store(expected, 0x40, hash);
            }
            if (patch.destination != level.address + offsets[i] || patch.size != set.binding_layout.size()
                || std::memcmp(patch.bytes.data(), expected.data(), patch.size))
                return Status::failure(FailureCode::RestorePreflightFailed);
        }
        const auto& head = prepared.patches_[patch_index++];
        if (head.destination != level.address + 0x58 || head.size != sizeof(std::uintptr_t)
            || Field<std::uintptr_t>(head.bytes, 0) != (level.cooldown_order.empty() ? 0 : level.cooldown_order.front()))
            return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (const auto& tick : ticks_)
    {
        if (epoch_ != current.epoch_)
            for (const auto& prerequisite : tick.prerequisites)
                if (!WeakIsLive(base, prerequisite.data())) return Status::failure(FailureCode::GenerationMismatch);
        const auto& patch = prepared.patches_[patch_index++];
        auto expected = tick.binding_layout;
        const auto prerequisites = Field<std::uintptr_t>(patch.bytes, 0x20);
        if (!payload_matches(prerequisites, static_cast<std::size_t>(Field<int>(expected, 0x2c)) * 16,
            tick.prerequisites.data(), tick.prerequisites.size() * 16)) return Status::failure(FailureCode::RestorePreflightFailed);
        Store(expected, 0x20, prerequisites);
        if (epoch_ != current.epoch_)
            for (const auto offset : {0x10u, 0x14u})
            {
                std::int32_t stamp{};
                if (!RebaseEpochStamp(Field<std::int32_t>(expected, offset), epoch_, current.epoch_, stamp))
                    return Status::failure(FailureCode::RestorePreflightFailed);
                Store(expected, offset, stamp);
            }
        if (patch.destination != tick.address || patch.size != expected.size() || patch.bytes != expected)
            return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (std::size_t i = 0; i < prepared.excluded_count_; ++i) {
        const auto found = std::find_if(current.ticks_.begin(), current.ticks_.end(),
            [&](const Tick& item) { return item.address == prepared.excluded_ticks_[i]; });
        if (found == current.ticks_.end() || found->owner.object != prepared.excluded_owners_[i]
            || current.ticks_.size() + prepared.added_count_ != ticks_.size() + prepared.excluded_count_
            || std::any_of(ticks_.begin(), ticks_.end(), [&](const Tick& item) {
                return item.address == found->address || item.owner.object == found->owner.object;
            })) return Status::failure(FailureCode::RestorePreflightFailed);
        const auto flags = Field<std::uint8_t>(found->binding_layout, 0xc);
        const auto& patch = prepared.patches_[patch_index++];
        if (!(flags & 0x40) || patch.destination != found->address + 0xc || patch.size != 1
            || Field<std::uint8_t>(patch.previous, 0) != flags
            || Field<std::uint8_t>(patch.bytes, 0) != static_cast<std::uint8_t>(flags & ~0x40u))
            return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (std::size_t i = 0; i < prepared.patches_.size(); ++i)
    {
        const auto& patch = prepared.patches_[i];
        if (!Writable(patch.destination, patch.size)
            || !SameBytes(patch.destination, patch.previous.data(), patch.size))
            return Status::failure(FailureCode::RestorePreflightFailed);
        for (std::size_t j = 0; j < i; ++j)
            if (Overlap(patch.destination, patch.size, prepared.patches_[j].destination, prepared.patches_[j].size))
                return Status::failure(FailureCode::RestorePreflightFailed);
    }
    // Every retired allocation must have exclusive heap ownership. Reject
    // aliases to another backing buffer or to any live header/tick object.
    for (std::size_t i = 0; i < prepared.allocations_.size(); ++i)
    {
        const auto& allocation = prepared.allocations_[i];
        const auto address = reinterpret_cast<std::uintptr_t>(allocation.previous);
        if (bool(address) != bool(allocation.previous_requested) || bool(allocation.data) != bool(allocation.requested))
            return Status::failure(FailureCode::RestorePreflightFailed);
        for (const auto& patch : prepared.patches_)
            if (Overlap(address, allocation.previous_requested, patch.destination, patch.size))
                return Status::failure(FailureCode::RestorePreflightFailed);
        for (std::size_t j = 0; j < i; ++j)
            if (Overlap(address, allocation.previous_requested, reinterpret_cast<std::uintptr_t>(prepared.allocations_[j].previous),
                prepared.allocations_[j].previous_requested)) return Status::failure(FailureCode::RestorePreflightFailed);
    }

    prepared.published_fingerprint_ = prepared.storage_fingerprint();
    prepared.published_ = true; // Partial writes retain both backing graphs too.
    for (const auto& patch : prepared.patches_)
        if (!WriteBytes(patch.destination, patch.bytes.data(), patch.size))
        { status = Status::failure(FailureCode::RestoreWriteFailed); break; }
    if (status.ok()) status = ValidatePublishedBacking(base, world, prepared);
    if (status.ok()) return status;
    return UndoBacking(current, base, world, prepared).ok() ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplaySchedulerState::ValidatePrivateBacking() const noexcept
{
    // The image contains B's original native pointers. Headers may currently
    // publish A/C, but these retained allocation payloads must still equal B.
    for (const auto& level : levels_) for (const auto& set : level.sets) {
        const auto flags = Field<std::uintptr_t>(set.binding_layout, 0x20);
        const auto hash = Field<std::uintptr_t>(set.binding_layout, 0x40);
        if (!SameBytes(Field<std::uintptr_t>(set.binding_layout, 0), set.slots.data(), set.slots.size())
            || (flags && !SameBytes(flags, set.allocation_flags.data(), set.allocation_flags.size() * 4))
            || (hash && !SameBytes(hash, set.hash.data(), set.hash.size() * 4)))
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    for (const auto& tick : ticks_)
        if (!SameBytes(Field<std::uintptr_t>(tick.binding_layout, 0x20), tick.prerequisites.data(), tick.prerequisites.size() * 16))
            return Status::failure(FailureCode::RestoreVerificationFailed);
    return Status::success();
}

bool Sc6ReplaySchedulerState::PreparedRestore::PreviousFingerprint(std::uint64_t& output) const noexcept
{
    return FingerprintImages(patches_, allocations_, true, output);
}

bool Sc6ReplaySchedulerState::PreparedRestore::FingerprintImages(std::span<const Patch> patches,
    std::span<const Allocation> allocations, bool previous, std::uint64_t& output) noexcept
{
    output = 14695981039346656037ull;
    __try {
        for (const auto& patch : patches) {
            if (!previous) for (std::size_t i = 0; i < sizeof(patch.destination); ++i) {
                output ^= reinterpret_cast<const unsigned char*>(&patch.destination)[i]; output *= 1099511628211ull;
            }
            for (std::size_t i = 0; i < patch.size; ++i) {
                output ^= std::to_integer<unsigned char>((previous ? patch.previous : patch.bytes)[i]); output *= 1099511628211ull;
            }
        }
        for (const auto& allocation : allocations) {
            const auto* bytes = static_cast<const unsigned char*>(previous ? allocation.previous : allocation.data);
            const auto size = previous ? allocation.previous_requested : allocation.requested;
            for (std::size_t i = 0; i < size; ++i) { output ^= bytes[i]; output *= 1099511628211ull; }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplaySchedulerState::BeginExecution(std::uintptr_t base, void* world,
    PreparedRestore& prepared, std::size_t retirement_budget) const noexcept
{
    if (prepared.executing_ || prepared.allocation_ownership_lost_ || !prepared.previous_image_)
        return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidatePublishedBacking(base, world, prepared);
    if (status.ok()) status = prepared.previous_image_->ValidatePrivateBacking();
    if (!status.ok()) return status;
    if (retirement_budget < prepared.owned_bytes() || retirement_budget > SIZE_MAX - prepared.owned_bytes())
        return Status::failure(FailureCode::CapacityExceeded);
    if (!prepared.PreviousFingerprint(prepared.previous_fingerprint_)) return Status::failure(FailureCode::ContextUnavailable);
    for (auto& allocation : prepared.allocations_) {
        allocation.data = nullptr;
        allocation.requested = allocation.copied = allocation.charged = 0;
    }
    prepared.execution_reservation_ = retirement_budget;
    prepared.executing_ = true;
    return Status::success();
}

Status Sc6ReplaySchedulerState::SettleExecution(const Sc6ReplaySchedulerState& observed,
    std::uintptr_t base, void* world, PreparedRestore& prepared, RestoreSettlement purpose, PublishedFailure* failure) const noexcept
{
    const auto checking = [&](const char* check, std::uintptr_t address = 0, std::uintptr_t owner = 0) {
        if (failure) *failure = {check, address, owner};
    };
    checking("settlement_purpose");
    if(purpose!=RestoreSettlement::RecoverOriginal && purpose!=RestoreSettlement::CommitCurrent)
        return Status::failure(FailureCode::IllegalTransition);
    const auto* original = prepared.previous_image_;
    checking("settlement_identity_counts");
    if (!prepared.executing_ || prepared.execution_settled_ || prepared.execution_undo_started_
        || !prepared.published_ || !original || prepared.base_ != base || prepared.world_ != world
        || prepared.thread_ != GetCurrentThreadId() || prepared.target_epoch_ != epoch_
        || observed.world_.object != original->world_.object || observed.world_.weak != original->world_.weak
        || observed.scene_.scene != original->scene_.scene || observed.scene_.control != original->scene_.control
        || observed.levels_.size() != original->levels_.size() || observed.ticks_.size() < original->ticks_.size()
        || (purpose == RestoreSettlement::RecoverOriginal && observed.ticks_.size() != original->ticks_.size()))
        return Status::failure(FailureCode::GenerationMismatch);
    checking("observed_current_image");
    auto status = observed.ValidateCurrentImage(base, world);
    if (status.ok()) { checking("original_bindings"); status = original->ValidateBindings(base, world); }
    if (status.ok()) { checking("birth_lifetimes"); status = prepared.birth_.ValidateLifetimes(); }
    if (status.ok()) { checking("private_owners"); status = prepared.ValidatePrivateOwners(); }
    if (status.ok()) { checking("excluded_ticks"); status = ValidateExcludedTick(prepared, false); }
    std::uint64_t before{};
    if (!status.ok()) return status;
    checking("previous_fingerprint");
    if (!prepared.PreviousFingerprint(before) || before != prepared.previous_fingerprint_)
        return Status::failure(FailureCode::RestoreVerificationFailed);
    try {
        std::vector<PreparedRestore::Patch> patches;
        std::vector<PreparedRestore::Allocation> allocations;
        const auto patch_count = observed.levels_.size() * 4 + observed.ticks_.size();
        const auto allocation_count = prepared.allocations_.size() + observed.levels_.size() * 9 + observed.ticks_.size();
        auto remaining = prepared.execution_reservation_;
        const auto charge = [&](std::size_t bytes) { if (bytes > remaining) return false; remaining -= bytes; return true; };
        if (patch_count > remaining / sizeof(PreparedRestore::Patch)) return Status::failure(FailureCode::CapacityExceeded);
        patches.reserve(patch_count);
        if (!charge(patches.capacity() * sizeof(PreparedRestore::Patch))
            || allocation_count > remaining / sizeof(PreparedRestore::Allocation)) return Status::failure(FailureCode::CapacityExceeded);
        allocations.reserve(allocation_count);
        if (!charge(allocations.capacity() * sizeof(PreparedRestore::Allocation))) return Status::failure(FailureCode::CapacityExceeded);
        // Old A addresses were relinquished; these records retain only B.
        allocations.insert(allocations.end(), prepared.allocations_.begin(), prepared.allocations_.end());
        const auto append = [&](std::uintptr_t address, std::size_t bytes) {
            if (bool(address) != bool(bytes)) return false;
            if (!bytes) return true;
            const auto native = reinterpret_cast<std::size_t(*)(std::size_t, unsigned)>(base + 0xd50dc0)(bytes, 0);
            if (native < bytes || !charge(native)) return false;
            allocations.push_back({reinterpret_cast<void*>(address), nullptr, bytes, bytes, native, 0, 0});
            return true;
        };
        for (const auto& b : original->levels_) {
            checking("original_level_in_C", b.address, b.owner.object);
            const auto c = std::find_if(observed.levels_.begin(), observed.levels_.end(), [&](const auto& x) { return x.address == b.address; });
            if (c == observed.levels_.end() || c->owner.object != b.owner.object || c->owner.weak != b.owner.weak)
                return Status::failure(FailureCode::GenerationMismatch);
            constexpr std::array<std::size_t, 3> offsets{8, 0x60, 0xc0};
            for (unsigned i = 0; i < 3; ++i) {
                auto& patch = patches.emplace_back(); patch.destination = b.address + offsets[i]; patch.size = 0x50;
                std::memcpy(patch.previous.data(), b.sets[i].binding_layout.data(), 0x50);
                std::memcpy(patch.bytes.data(), c->sets[i].binding_layout.data(), 0x50);
                const auto& h = c->sets[i].binding_layout;
                if (!append(Field<std::uintptr_t>(h, 0), std::size_t(Field<int>(h, 0xc)) * 16)
                    || (Field<std::uintptr_t>(h, 0x20) && !append(Field<std::uintptr_t>(h, 0x20), (std::size_t(Field<int>(h, 0x2c)) + 31) / 32 * 4))
                    || (Field<std::uintptr_t>(h, 0x40) && !append(Field<std::uintptr_t>(h, 0x40), std::size_t(Field<int>(h, 0x48)) * 4)))
                    return Status::failure(FailureCode::CapacityExceeded);
            }
            auto& head = patches.emplace_back(); head.destination = b.address + 0x58; head.size = 8;
            Store(head.previous, 0, b.cooldown_order.empty() ? std::uintptr_t{} : b.cooldown_order.front());
            Store(head.bytes, 0, c->cooldown_order.empty() ? std::uintptr_t{} : c->cooldown_order.front());
        }
        for (const auto& b : original->ticks_) {
            checking("original_tick_in_C", b.address, b.owner.object);
            const auto c = std::find_if(observed.ticks_.begin(), observed.ticks_.end(), [&](const auto& x) { return x.address == b.address; });
            if (c == observed.ticks_.end() || c->owner.object != b.owner.object || c->owner.weak != b.owner.weak
                || std::memcmp(c->binding_layout.data() + 0xe, b.binding_layout.data() + 0xe, 2)
                || std::memcmp(c->binding_layout.data() + 0x44, b.binding_layout.data() + 0x44, 4))
                return Status::failure(FailureCode::GenerationMismatch);
            auto& patch = patches.emplace_back(); patch.destination = b.address; patch.size = 0x58;
            patch.previous = b.binding_layout; patch.bytes = c->binding_layout;
            if (original->epoch_ != observed.epoch_) for (const auto offset : {0x10u, 0x14u}) {
                std::int32_t stamp{};
                if (!RebaseEpochStamp(Field<std::int32_t>(b.binding_layout, offset), original->epoch_, observed.epoch_, stamp))
                    return Status::failure(FailureCode::UnsupportedContent);
                Store(patch.previous, offset, stamp);
            }
            const bool excluded = std::find(prepared.excluded_ticks_.begin(), prepared.excluded_ticks_.begin() + prepared.excluded_count_, b.address)
                != prepared.excluded_ticks_.begin() + prepared.excluded_count_;
            // Quarantined B ticks retain their original prerequisite backing.
            // It was never handed to C and must not be freed during undo.
            if (!excluded && !append(Field<std::uintptr_t>(c->binding_layout, 0x20), std::size_t(Field<int>(c->binding_layout, 0x2c)) * 16))
                return Status::failure(FailureCode::CapacityExceeded);
        }
        for (const auto& a : allocations) if (a.data) {
            const auto address = reinterpret_cast<std::uintptr_t>(a.data);
            for (const auto& patch : patches)
                if (Overlap(address, a.requested, patch.destination, patch.size)) return Status::failure(FailureCode::RestorePreflightFailed);
            for (const auto& other : allocations) {
                if (Overlap(address, a.requested, reinterpret_cast<std::uintptr_t>(other.previous), other.previous_requested)
                    || (&a != &other && Overlap(address, a.requested, reinterpret_cast<std::uintptr_t>(other.data), other.requested)))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            }
            for (std::size_t i = 0; i < prepared.excluded_count_; ++i) {
                const auto tick = std::find_if(original->ticks_.begin(), original->ticks_.end(), [&](const auto& x) { return x.address == prepared.excluded_ticks_[i]; });
                if (tick == original->ticks_.end() || Overlap(address, a.requested, Field<std::uintptr_t>(tick->binding_layout, 0x20),
                    std::size_t(Field<int>(tick->binding_layout, 0x2c)) * 16)) return Status::failure(FailureCode::RestorePreflightFailed);
            }
        }
        // Additional C ticks stay owned by their native components on commit.
        // They must not borrow any displaced B allocation which commit frees.
        for (const auto& tick : observed.ticks_) {
            if (std::any_of(original->ticks_.begin(), original->ticks_.end(), [&](const auto& old) {return old.address==tick.address;})) continue;
            const auto pointer=Field<std::uintptr_t>(tick.binding_layout,0x20);
            const auto size=std::size_t(Field<int>(tick.binding_layout,0x2c))*16;
            for (const auto& allocation : allocations)
                if (Overlap(pointer,size,reinterpret_cast<std::uintptr_t>(allocation.previous),allocation.previous_requested))
                    return Status::failure(FailureCode::RestorePreflightFailed);
        }
        std::uint64_t b_fingerprint{}, c_fingerprint{};
        if (!PreparedRestore::FingerprintImages(patches, allocations, true, b_fingerprint)
            || !PreparedRestore::FingerprintImages(patches, allocations, false, c_fingerprint))
            return Status::failure(FailureCode::ContextUnavailable);
        prepared.allocations_.swap(allocations); prepared.patches_.swap(patches);
        prepared.execution_reservation_ = remaining;
        prepared.epoch_ = observed.epoch_; prepared.execution_image_ = &observed;
        prepared.previous_fingerprint_ = b_fingerprint;
        prepared.published_fingerprint_ = c_fingerprint;
        prepared.execution_settled_ = true;
        checking("settled_published_backing");
        return ValidatePublishedBacking(base, world, prepared);
    } catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplaySchedulerState::PreparedRestore::ReopenExecutionForUndo(const Sc6ReplaySchedulerState& target) noexcept
{
    if(!executing_ || !published_ || execution_undo_started_ || !previous_image_)
        return Status::failure(FailureCode::IllegalTransition);
    if(!execution_settled_)return Status::success();
    if(!execution_image_)return Status::failure(FailureCode::IllegalTransition);
    auto status=target.ValidatePublishedBacking(base_,world_,*this);if(!status.ok())return status;
    std::uint64_t before{};
    if(!PreviousFingerprint(before) || before!=previous_fingerprint_)
        return Status::failure(FailureCode::RestoreVerificationFailed);
    std::size_t released{};
    for(const auto& allocation:allocations_) {
        if(allocation.charged>SIZE_MAX-released)return Status::failure(FailureCode::CapacityExceeded);
        released+=allocation.charged;
    }
    if(released>SIZE_MAX-execution_reservation_)return Status::failure(FailureCode::CapacityExceeded);
    execution_reservation_+=released;
    for(auto& allocation:allocations_) {
        allocation.data=nullptr;allocation.requested=allocation.copied=allocation.charged=0;
    }
    std::erase_if(allocations_,[](const auto& allocation){return !allocation.previous;});
    execution_image_=nullptr;execution_settled_=false;
    return Status::success();
}

Status Sc6ReplaySchedulerState::FinalizeContainerOrder(std::uintptr_t base, void* world,
    PreparedRestore& prepared, PublishedFailure* failure) const noexcept
{
    if (failure) *failure = {};
    const auto reject = [&](const char* reason, std::uintptr_t address) {
        if (failure) *failure = {reason, address};
        return Status::failure(FailureCode::RestoreVerificationFailed);
    };
    if (prepared.executing_ || !prepared.ready_ || !prepared.published_ || prepared.base_ != base || prepared.world_ != world
        || prepared.thread_ != GetCurrentThreadId() || prepared.target_epoch_ != epoch_
        || !SameBytes(base + 0x4197170, &prepared.epoch_, 8) || !ValidateBindings(base, world).ok())
        return reject("finalize_binding", 0);
    // 1403C90B0 hides/disables creation meshes, then shows/enables selected
    // meshes. 142167500 removes/reinserts registered ticks. Preserve those
    // callbacks; normalize storage order only after proving the resulting
    // tick values and logical memberships already equal captured A.
    __try {
        for (std::size_t i = 0; i < prepared.patches_.size(); ++i)
            if (i >= levels_.size() * 4 || i % 4 == 3) {
                const auto& patch = prepared.patches_[i];
                if (!SameBytes(patch.destination, patch.bytes.data(), patch.size)) {
                    const auto result = reject("finalize_tick_or_cooldown", patch.destination);
                    if (failure) {
                        for (const auto& tick : ticks_) if (tick.address == patch.destination) failure->owner = tick.owner.object;
                        for (std::size_t n = 0; n < patch.size; ++n)
                            if (reinterpret_cast<const std::byte*>(patch.destination)[n] != patch.bytes[n]) {
                                failure->offset = n; failure->expected = std::to_integer<unsigned>(patch.bytes[n]);
                                failure->observed = std::to_integer<unsigned>(reinterpret_cast<const std::byte*>(patch.destination)[n]); break;
                            }
                    }
                    return result;
                }
            }
        if (prepared.excluded_count_ && (!prepared.birth_.ValidateOwners().ok()
            || !ValidateExcludedTick(prepared, false).ok())) return reject("finalize_birth", prepared.excluded_ticks_[0]);
        for (std::size_t l = 0; l < levels_.size(); ++l) for (unsigned s = 0; s < 3; ++s) {
            const auto& set = levels_[l].sets[s];
            const auto& patch = prepared.patches_[l * 4 + s];
            std::array<std::byte, 0x50> live{};
            if (!WriteBytes(reinterpret_cast<std::uintptr_t>(live.data()), reinterpret_cast<void*>(patch.destination), live.size()))
                return reject("finalize_set_read", patch.destination);
            for (unsigned offset : {0u, 0x20u, 0x40u})
                if (Field<std::uintptr_t>(live, offset) != Field<std::uintptr_t>(patch.bytes, offset)) {
                    // Native reallocation may have retired a tracked pointer.
                    // Never free it through stale metadata, even after B undo.
                    prepared.allocation_ownership_lost_ = true;
                    return reject("finalize_set_allocation", patch.destination + offset);
                }
            for (unsigned offset : {0xcu, 0x2cu, 0x48u})
                if (Field<int>(live, offset) != Field<int>(patch.bytes, offset))
                    return reject("finalize_set_capacity", patch.destination + offset);
            const auto count = Field<int>(live, 8), capacity = Field<int>(live, 0xc);
            const auto free = Field<int>(live, 0x34), max_bits = Field<int>(live, 0x2c);
            if (count < 0 || count > capacity || count > max_bits || Field<int>(live, 0x28) != count
                || free < 0 || free > count || (Field<std::uintptr_t>(live, 0x20) == 0 && max_bits > 128))
                return reject("finalize_set_extent", patch.destination);
            const auto* flags = Field<std::uintptr_t>(live, 0x20)
                ? reinterpret_cast<const unsigned*>(Field<std::uintptr_t>(live, 0x20))
                : reinterpret_cast<const unsigned*>(live.data() + 0x10);
            const auto* slots = reinterpret_cast<const std::uintptr_t*>(Field<std::uintptr_t>(live, 0));
            int occupied{};
            for (int n = 0; n < count; ++n) if ((flags[n / 32] >> (n % 32)) & 1) ++occupied;
            const auto expected_count = Field<int>(set.binding_layout, 8);
            if (occupied != count - free || occupied != expected_count - Field<int>(set.binding_layout, 0x34))
                return reject("finalize_set_members", patch.destination);
            for (int n = 0; n < expected_count; ++n) if ((set.allocation_flags[n / 32] >> (n % 32)) & 1) {
                std::uintptr_t expected{};
                std::memcpy(&expected, set.slots.data() + n * 16, 8);
                unsigned found{};
                for (int j = 0; j < count; ++j) if (((flags[j / 32] >> (j % 32)) & 1) && slots[j * 2] == expected) ++found;
                if (found != 1) return reject("finalize_set_member", expected);
            }
        }
        // All owners and memberships pass before any representation write.
        // Only private set backing is rebuilt; tick fields/gameplay are untouched.
        for (std::size_t l = 0; l < levels_.size(); ++l) for (unsigned s = 0; s < 3; ++s) {
            const auto& set = levels_[l].sets[s];
            const auto& patch = prepared.patches_[l * 4 + s];
            for (unsigned offset : {0u, 0x20u, 0x40u}) {
                const auto address = Field<std::uintptr_t>(patch.bytes, offset);
                if (!address) continue;
                const auto allocation = std::find_if(prepared.allocations_.begin(), prepared.allocations_.end(),
                    [&](const auto& item) { return reinterpret_cast<std::uintptr_t>(item.data) == address; });
                if (allocation == prepared.allocations_.end()) return reject("finalize_owned_storage", address);
                const void* source = offset == 0 ? static_cast<const void*>(set.slots.data())
                    : offset == 0x20 ? static_cast<const void*>(set.allocation_flags.data()) : set.hash.data();
                const auto size = offset == 0 ? set.slots.size() : offset == 0x20 ? set.allocation_flags.size() * 4 : set.hash.size() * 4;
                if (size > allocation->requested) return reject("finalize_owned_extent", address);
                std::memset(allocation->data, 0, allocation->requested);
                if (size) std::memcpy(allocation->data, source, size);
            }
            if (!WriteBytes(patch.destination, patch.bytes.data(), patch.size)) return reject("finalize_write", patch.destination);
        }
        return ValidatePublishedBacking(base, world, prepared, failure);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplaySchedulerState::ValidatePublishedBacking(std::uintptr_t base, void* world,
    const PreparedRestore& prepared, PublishedFailure* failure) const noexcept
{
    if (failure) *failure = {};
    if (!prepared.ready_ || !prepared.published_ || prepared.thread_ != GetCurrentThreadId())
        return Status::failure(FailureCode::IllegalTransition);
    if (prepared.base_ != base || prepared.world_ != world || prepared.target_epoch_ != epoch_
        || !SameBytes(base + 0x4197170, &prepared.epoch_, sizeof(prepared.epoch_)))
        return Status::failure(FailureCode::GenerationMismatch);
    if (prepared.executing_) {
        // A-only owners may have completed native destruction during C.
        // Execution handed their storage to native code. Validate the settled
        // current image and protected B, never the historical A owner graph.
        if (!prepared.execution_settled_ || prepared.execution_undo_started_ || !prepared.execution_image_
            || !prepared.previous_image_ || !prepared.execution_image_->ValidateCurrentImage(base, world).ok()
            || !prepared.previous_image_->ValidateBindings(base, world).ok()
            || (!prepared.birth_.ValidateLifetimes().ok() || !prepared.ValidatePrivateOwners().ok())) return Status::failure(FailureCode::RestoreVerificationFailed);
        std::uint64_t original{}, observed{};
        if (!prepared.PreviousFingerprint(original) || original != prepared.previous_fingerprint_
            || !PreparedRestore::FingerprintImages(prepared.patches_, prepared.allocations_, false, observed)
            || observed != prepared.published_fingerprint_) return Status::failure(FailureCode::RestoreVerificationFailed);
        return Status::success();
    }
    const auto status = ValidateBindings(base, world);
    if (!status.ok()) return status;
    if (prepared.excluded_count_)
    {
        const auto retained = prepared.birth_.ValidateOwners();
        if (!retained.ok()) return retained;
        const auto tick = ValidateExcludedTick(prepared, false);
        if (!tick.ok()) { if (failure) *failure = {"excluded_tick", prepared.excluded_ticks_[0], prepared.excluded_owners_[0]}; return tick; }
    }
    // Check native pointer-bearing headers before reading retained backing;
    // accidental scheduler advancement/reallocation invalidates admission.
    for (const auto& patch : prepared.patches_)
        if (!SameBytes(patch.destination, patch.bytes.data(), patch.size)) {
            if (failure) {
                *failure = {"published_patch", patch.destination};
                for (const auto& tick : ticks_) if (tick.address == patch.destination) failure->owner = tick.owner.object;
                std::array<std::byte, 0x58> live{};
                if (WriteBytes(reinterpret_cast<std::uintptr_t>(live.data()), reinterpret_cast<const void*>(patch.destination), patch.size)) for (std::size_t i = 0; i < patch.size; ++i)
                    if (live[i] != patch.bytes[i]) {
                        failure->offset = i; failure->expected = std::to_integer<unsigned>(patch.bytes[i]);
                        failure->observed = std::to_integer<unsigned>(live[i]); break;
                    }
            }
            return Status::failure(FailureCode::RestoreVerificationFailed);
        }
    __try
    {
        if (failure) *failure = {"backing_fingerprint"};
        return prepared.storage_fingerprint() == prepared.published_fingerprint_
            ? Status::success() : Status::failure(FailureCode::RestoreVerificationFailed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplaySchedulerState::PreparedRestore::ValidatePrivateOwners() const noexcept
{
    const auto births=birth_.members().size();
    if(excluded_count_<births || excluded_count_>excluded_owners_.size())return Status::failure(FailureCode::GenerationMismatch);
    for(std::size_t i=0;i<births;++i)
        if(excluded_owners_[i]!=birth_.members()[i].component())return Status::failure(FailureCode::GenerationMismatch);
    if(!private_owners_.visit)return !private_owners_.context && excluded_count_==births
        ?Status::success():Status::failure(FailureCode::GenerationMismatch);
    struct Check {const PreparedRestore& p;std::size_t next;} check{*this,births};
    const auto status=private_owners_.visit(private_owners_.context,&check,[](void* context,std::uintptr_t owner) {
        auto& c=*static_cast<Check*>(context);
        return c.next<c.p.excluded_count_ && c.p.excluded_owners_[c.next++]==owner;
    });
    return status.ok() && check.next==excluded_count_?Status::success():Status::failure(FailureCode::GenerationMismatch);
}

Status Sc6ReplaySchedulerState::ValidateExcludedTick(const PreparedRestore& prepared, bool registered) const noexcept
{
    if (!prepared.excluded_count_) return Status::success();
    const auto* previous = prepared.previous_image_;
    // Quarantined B ticks did not execute. C settlement updates epoch_ for
    // eventual undo admission, but their actual frozen prefix still belongs
    // to original_epoch_. Reopening C must not reinterpret that B binding.
    const auto retained_epoch=prepared.executing_?prepared.original_epoch_:prepared.epoch_;
    if (!previous || previous->base_ != prepared.base_ || previous->epoch_ != retained_epoch
        || !prepared.ValidatePrivateOwners().ok())
        return Status::failure(FailureCode::GenerationMismatch);
    for (std::size_t i = 0; i < prepared.excluded_count_; ++i) {
        const auto found = std::find_if(previous->ticks_.begin(), previous->ticks_.end(),
            [&](const Tick& tick) { return tick.address == prepared.excluded_ticks_[i]; });
        if (found == previous->ticks_.end() || found->owner.object != prepared.excluded_owners_[i]
            || !previous->IsLiveObject(found->owner)) return Status::failure(FailureCode::GenerationMismatch);
        auto expected = found->binding_layout;
        if (!registered) Store(expected, 0xc, static_cast<std::uint8_t>(Field<std::uint8_t>(expected, 0xc) & ~0x40u));
        if (!SameBytes(found->address, expected.data(), expected.size())
            || !SameBytes(Field<std::uintptr_t>(expected, 0x20), found->prerequisites.data(), found->prerequisites.size() * 16))
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    return Status::success();
}

Status Sc6ReplaySchedulerState::UndoBacking(const Sc6ReplaySchedulerState& current,
    std::uintptr_t base, void* world, PreparedRestore& prepared) const noexcept
{
    if (!prepared.published_) return Status::success();
    if (prepared.executing_) {
        if (!prepared.ready_ || !prepared.execution_settled_ || prepared.previous_image_ != &current
            || prepared.thread_ != GetCurrentThreadId() || prepared.base_ != base || prepared.world_ != world
            || prepared.target_epoch_ != epoch_ || prepared.original_epoch_ != current.epoch_
            || !SameBytes(base + 0x4197170, &prepared.epoch_, sizeof(prepared.epoch_))
            || !current.ValidateBindings(base, world).ok() || (!prepared.birth_.ValidateLifetimes().ok() || !prepared.ValidatePrivateOwners().ok()))
            return Status::failure(FailureCode::UndoFailed);
        std::uint64_t original{};
        if (!prepared.PreviousFingerprint(original) || original != prepared.previous_fingerprint_
            || (!prepared.execution_undo_started_ && !ValidatePublishedBacking(base, world, prepared).ok()))
            return Status::failure(FailureCode::UndoFailed);
        prepared.execution_undo_started_ = true;
        for (auto i = prepared.patches_.size(); i > 0; --i) {
            const auto& patch = prepared.patches_[i - 1];
            if (!WriteBytes(patch.destination, patch.previous.data(), patch.size)) return Status::failure(FailureCode::UndoFailed);
        }
        // B's headers point at its original allocations. Only the two native
        // admission stamps were rebased from B's captured predicate to the
        // current physical epoch; no engine counter or expected observation is
        // installed. Verify every destination and original backing before C
        // becomes eligible for release by the enclosing owner.
        for (const auto& patch : prepared.patches_)
            if (!SameBytes(patch.destination, patch.previous.data(), patch.size)) return Status::failure(FailureCode::UndoFailed);
        if (!current.ValidatePrivateBacking().ok() || !prepared.PreviousFingerprint(original)
            || original != prepared.previous_fingerprint_ || !current.ValidateBindings(base, world).ok())
            return Status::failure(FailureCode::UndoFailed);
        prepared.published_ = false; prepared.published_fingerprint_ = 0;
        return Status::success();
    }
    if (!prepared.ready_ || prepared.thread_ != GetCurrentThreadId()
        || prepared.base_ != base || prepared.world_ != world || prepared.target_epoch_ != epoch_
        || prepared.epoch_ != current.epoch_
        || !SameBytes(base + 0x4197170, &prepared.epoch_, sizeof(prepared.epoch_)))
        return Status::failure(FailureCode::UndoFailed);
    if (!current.ValidateBindings(base, world).ok()) return Status::failure(FailureCode::UndoFailed);
    if (prepared.excluded_count_ && (!prepared.birth_.ValidateOwners().ok() || !prepared.ValidatePrivateOwners().ok()))
        return Status::failure(FailureCode::UndoFailed);
    bool recovered = true;
    for (auto i = prepared.patches_.size(); i > 0; --i)
    {
        const auto& patch = prepared.patches_[i - 1];
        if (!WriteBytes(patch.destination, patch.previous.data(), patch.size)) recovered = false;
    }
    // Verify B's backing contents as well as the restored headers. Failure
    // retains the complete prepared graph and refuses commit/reclamation.
    if (!recovered || !current.ValidateCurrentImage(base, world).ok())
        return Status::failure(FailureCode::UndoFailed);
    prepared.published_ = false;
    prepared.published_fingerprint_ = 0;
    return Status::success();
}

Status Sc6ReplaySchedulerState::CommitBacking(std::uintptr_t base, void* world, PreparedRestore& prepared) const noexcept
{
    const auto status = ValidatePublishedBacking(base, world, prepared);
    if (!status.ok()) return status;
    // This is the enclosing transaction's final irreversible retirement step.
    // It does not invoke destructors that unregister ticks or destroy levels.
    for (auto& allocation : prepared.allocations_) std::swap(allocation.data, allocation.previous);
    prepared.published_ = false;
    prepared.Clear();
    return Status::success();
}

Status Sc6ReplaySchedulerState::RestoreBacking(const Sc6ReplaySchedulerState& current,
    std::uintptr_t base, void* world, std::size_t budget, PreparedRestore& prepared,
    CommitQuery cancel_before_commit, void* context) const noexcept
{
    // Compatibility for the maintained standalone scheduler experiment. The
    // enclosing world transaction uses the split operations above instead.
    auto status = PublishBacking(current, base, world, budget, prepared);
    if (!status.ok()) return status;
    if (cancel_before_commit && cancel_before_commit(context)) status = Status::failure(FailureCode::Cancelled);
    else status = CommitBacking(base, world, prepared);
    if (status.ok()) return status;
    return UndoBacking(current, base, world, prepared).ok() ? status : Status::failure(FailureCode::UndoFailed);
}

std::uint64_t Sc6ReplaySchedulerState::PreparedRestore::storage_fingerprint() const noexcept
{
    auto result = std::uint64_t{14695981039346656037ull};
    const auto add = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) { result ^= bytes[i]; result *= 1099511628211ull; }
    };
    for (const auto& patch : patches_) { add(&patch.destination, sizeof(patch.destination)); add(patch.bytes.data(), patch.size); }
    for (const auto& allocation : allocations_) add(allocation.data, allocation.requested);
    return result; // Buffer-retention diagnostic, never a canonical state hash.
}
}
