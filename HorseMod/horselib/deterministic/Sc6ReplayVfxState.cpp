#include "Sc6ReplayVfxState.hpp"
#include <Windows.h>
#include <cstring>
#include <utility>
#include <algorithm>

namespace Horse::Deterministic
{
namespace
{
template<class T> T At(const void* p, std::size_t offset)
{ T result; std::memcpy(&result, static_cast<const std::byte*>(p) + offset, sizeof(result)); return result; }
const std::byte* Provider(const std::byte* request)
{ return At<const std::byte*>(request, 0x90); }
bool Live(std::uintptr_t base, const std::array<std::int32_t, 2>& weak, std::uintptr_t object) noexcept
{
    __try { return object && reinterpret_cast<std::uintptr_t (*)(const void*)>(base + 0xf823f0)(weak.data()) == object; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Bind(std::uintptr_t base, std::array<std::int32_t, 2>& weak, std::uintptr_t object) noexcept
{
    __try
    {
        if (!object) return false;
        reinterpret_cast<void (*)(void*, const void*)>(base + 0xf7bad0)(weak.data(), reinterpret_cast<void*>(object));
        return Live(base, weak, object);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CopyBytes(void* target, const void* source, std::size_t bytes) noexcept
{
    __try { std::memcpy(target, source, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool SameIdentity(const void* a, const void* b) noexcept
{
    __try { return std::memcmp(a, b, 12) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool WritableRange(std::uintptr_t address, std::size_t bytes) noexcept
{
    if (!address || bytes > UINTPTR_MAX - address) return false;
    const auto end = address + bytes;
    while (address < end)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))
            || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto access = region.Protect & 0xff;
        if (access != PAGE_READWRITE && access != PAGE_WRITECOPY
            && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > UINTPTR_MAX - begin) return false;
        const auto next = begin + region.RegionSize;
        if (next <= address) return false;
        address = next;
    }
    return true;
}
bool Overlaps(std::uintptr_t a, std::size_t an, std::uintptr_t b, std::size_t bn) noexcept
{
    if (!an || !bn) return false;
    if (!a || !b || an > UINTPTR_MAX - a || bn > UINTPTR_MAX - b) return true;
    return a < b + bn && b < a + an;
}
}

Sc6ReplayVfxState::~Sc6ReplayVfxState()
{
    for (std::size_t i = 0; i < constructed_; ++i)
    {
        auto* provider = slots_[i].bytes.data() + 0x80;
        // The request owns only this 0x38-byte small-array subobject. The
        // unified timer-delegate destructor is larger and is NOT applicable.
        reinterpret_cast<void (*)(void*)>(base_ + 0x3a22d0)(provider);
        if (auto* heap = At<void*>(provider, 0x20))
            reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(heap);
    }
}

void Sc6ReplayVfxState::Swap(Sc6ReplayVfxState& other) noexcept
{
    using std::swap;
    swap(base_, other.base_); swap(manager_, other.manager_); swap(manager_weak_, other.manager_weak_);
    swap(thread_, other.thread_); swap(sequence_, other.sequence_); swap(counts_, other.counts_);
    swap(slot_capacities_, other.slot_capacities_);
    swap(slots_, other.slots_); swap(capacity_, other.capacity_); swap(constructed_, other.constructed_);
    swap(provider_bytes_, other.provider_bytes_); swap(valid_, other.valid_);
    swap(tables_, other.tables_);
    swap(definition_layout_, other.definition_layout_);
    swap(definition_storage_, other.definition_storage_);
    swap(tile_pools_, other.tile_pools_);
    swap(cpu_emitters_, other.cpu_emitters_);
    swap(components_, other.components_);
    swap(gpu_owners_, other.gpu_owners_);
    swap(object_lease_, other.object_lease_);
    swap(reconstruction_lease_, other.reconstruction_lease_);
}

std::size_t Sc6ReplayVfxState::owned_bytes() const noexcept
{
    auto bytes = capacity_ * sizeof(Slot) + provider_bytes_ + components_.capacity() * sizeof(ComponentBinding);
    for (std::size_t i = 0; i < tables_.size(); ++i) bytes += tables_[i].count * table_strides[i];
    for (std::size_t i = 0; i < definition_storage_.size(); ++i)
        bytes += definition_storage_[i].count * definition_strides[i];
    bytes += tile_pools_.capacity() * sizeof(TilePool);
    for (const auto& pool : tile_pools_) bytes += pool.count * sizeof(std::uint32_t);
    bytes += cpu_emitters_.capacity() * sizeof(CpuEmitter);
    for (const auto& emitter : cpu_emitters_) bytes += emitter.image.owned_bytes() - sizeof(Sc6ReplayCpuEmitterState);
    for (const auto& component : components_) bytes += component.emitters.capacity() * sizeof(std::uintptr_t);
    bytes += gpu_owners_.capacity() * sizeof(GpuOwner);
    for (const auto& owner : gpu_owners_) bytes += owner.tiles.capacity() * sizeof(std::uint32_t)
        + owner.image.owned_bytes() - sizeof(Sc6ReplayCpuEmitterState);
    if (object_lease_) bytes += object_lease_->owned_bytes();
    if (reconstruction_lease_) bytes += reconstruction_lease_->owned_bytes();
    return bytes;
}

Status Sc6ReplayVfxState::ReadTilePool(std::uintptr_t address, std::uint32_t* output,
    std::size_t capacity, std::size_t& count, bool compare) noexcept
{
    // 141F8E0E0 constructs 65,536 indices, count and a native critical section
    // at pool+190/+40190/+40198. Never serialize or replace that lock.
    __try
    {
        auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(address + 0x40198);
        if (!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::CapturePreflightFailed);
        __try
        {
            const auto live_count = At<int>(reinterpret_cast<void*>(address), 0x40190);
            if (live_count < 0 || live_count > 65536) return Status::failure(FailureCode::CapturePreflightFailed);
            count = static_cast<std::size_t>(live_count);
            if (!output) return Status::success();
            if (count != capacity) return Status::failure(FailureCode::GenerationMismatch);
            const auto* source = reinterpret_cast<const void*>(address + 0x190);
            if (compare)
                return std::memcmp(output, source, count * 4) ? Status::failure(FailureCode::GenerationMismatch) : Status::success();
            std::memcpy(output, source, count * 4);
            return Status::success();
        }
        __finally { LeaveCriticalSection(lock); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ValidateNativeTilePartition(std::uintptr_t pool,
    const std::array<std::uint64_t, 1024>& owned, std::size_t& free_count) noexcept
{
    if (!pool) return Status::failure(FailureCode::ContextUnavailable);
    __try
    {
        auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(pool + 0x40198);
        if (!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::CapturePreflightFailed);
        __try
        {
            const auto count = At<int>(reinterpret_cast<void*>(pool), 0x40190);
            if (count < 0 || count > 65536) return Status::failure(FailureCode::CapturePreflightFailed);
            free_count = static_cast<std::size_t>(count);
            return IsCompleteTilePartition(owned, {reinterpret_cast<const std::uint32_t*>(pool + 0x190), free_count})
                ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
        }
        __finally { LeaveCriticalSection(lock); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

bool Sc6ReplayVfxState::ReadMaterialRoots(std::uintptr_t base,std::uintptr_t component,
    std::array<MaterialRoots,2>& output) noexcept
{
    output={};
    __try {
        for(unsigned side=0;side<2;++side) {
            auto& row=output[side];const auto* header=reinterpret_cast<const void*>(component+0xa98+side*16);
            row.data=At<std::uintptr_t>(header,0);row.count=At<int>(header,8);row.capacity=At<int>(header,12);
            if(row.count<0 || row.count>32 || row.capacity<row.count || row.capacity>64
                || bool(row.data)!=bool(row.capacity) || (row.data && row.data+row.capacity*8<row.data))return false;
            for(int i=0;i<row.count;++i) {
                auto& entry=row.entries[i];entry.object=At<std::uintptr_t>(reinterpret_cast<void*>(row.data),i*8);
                if(!entry.object)continue;
                const auto* material=reinterpret_cast<void*>(entry.object);
                // Exact native MID constructor141F25600 and outer selection
                // 141EC5E60/141F2F530. Native destruction only unroots these
                // entries; their leases survive until the ordered render drain.
                if(At<std::uintptr_t>(material,0)!=base+0x391ee70
                    || At<std::uintptr_t>(material,0x20)!=component)return false;
                entry.parent=At<std::uintptr_t>(material,0x78);if(!entry.parent)return false;
                for(unsigned previous=0;previous<=side;++previous)
                    for(int j=0;j<(previous==side?i:output[previous].count);++j)
                        if(output[previous].entries[j].object==entry.object)return false;
            }
        }
        const auto& a=output[0];const auto& b=output[1];
        return !a.data || !b.data || a.data+a.capacity*8<=b.data || b.data+b.capacity*8<=a.data;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

Status Sc6ReplayVfxState::ValidateCompletionOwnership(const void* context,CompletionOwnerCheck check,const char** failed_check) const noexcept
{
    const auto fail=[&](const char* why) {
        if(failed_check)*failed_check=why;
        return Status::failure(FailureCode::UnsupportedContent);
    };
    if(failed_check)*failed_check=nullptr;
    if(!valid_ || !check || !base_) return fail("completion_capture_context");
    // Native140F7D530 independently broadcasts global1440956C0 on activation
    // and finalization. Count+50 and recursion+64 are its consumed domain;
    // retaining component delegates does not own any listener registered here.
    // No array bytes or dormant allocation layout are historical state.
    std::int32_t count{},depth{};
    if(base_>UINTPTR_MAX-0x4095728
        || !CopyBytes(&count,reinterpret_cast<void*>(base_+0x4095710),4)
        || !CopyBytes(&depth,reinterpret_cast<void*>(base_+0x4095724),4))
        return fail("global_particle_callback_read");
    if(count) return fail("global_particle_callback_count");
    if(depth) return fail("global_particle_callback_depth");
    // All reads below are immutable captured bytes, not dead native arrays.
    // The callback independently verifies live receiver identity and native code.
    for(const auto& component:components_) {
        const auto& callbacks=component.completions;
        if(callbacks.count>callbacks.entries.size()) return fail("component_completion_capacity");
        for(std::size_t i=0;i<callbacks.count;++i) {
            const auto& row=callbacks.entries[i];
            std::array<std::int32_t,2> weak{};std::uint64_t name{};
            std::memcpy(weak.data(),row.data(),8);std::memcpy(&name,row.data()+8,8);
            if(weak!=manager_weak_ || !check(context,true,weak,name))
                return fail("component_completion_receiver");
            for(std::size_t j=0;j<i;++j) if(row==callbacks.entries[j])
                return fail("component_completion_duplicate");
        }
    }
    const auto& listeners=tables_[0];
    // Only the two captured fighter trace roots have an audited listener route.
    if(listeners.count>2 || (listeners.count && !listeners.bytes))
        return fail("manager_listener_capacity");
    for(std::size_t i=0;i<listeners.count;++i) {
        const auto* row=listeners.bytes.get()+i*16;
        std::array<std::int32_t,2> weak{};std::uint64_t name{};
        std::memcpy(weak.data(),row,8);std::memcpy(&name,row+8,8);
        if(!check(context,false,weak,name)) return fail("manager_listener_receiver");
        for(std::size_t j=0;j<i;++j) if(!std::memcmp(row,listeners.bytes.get()+j*16,16))
            return fail("manager_listener_duplicate");
    }
    return Status::success();
}

Status Sc6ReplayVfxState::ReadCompletionBindings(std::uintptr_t component,CompletionBindings& output) noexcept
{
    output={};
    Header header{};
    // Native event-manager dispatch broadcasts these independently of
    // OnSystemFinished. This common reader also covers persistent stage
    // components skipped by the detached reconstruction visitor. Until their
    // receiver-owned mutable state is supported, require empty routes both at
    // capture and at publication revalidation; never suppress dispatch.
    for(const auto offset:{0x850u,0x860u,0x870u,0x880u}) {
        Header events{};
        if(!component || component>UINTPTR_MAX-0x980
            || !CopyBytes(&events,reinterpret_cast<void*>(component+offset),sizeof(events)))
            return Status::failure(FailureCode::ContextUnavailable);
        if(events.count!=0 || events.capacity<0 || (events.capacity && !events.data))
            return Status::failure(FailureCode::UnsupportedContent);
    }
    if(!component || component>UINTPTR_MAX-0x980
        || !CopyBytes(&header,reinterpret_cast<void*>(component+0x970),sizeof(header)))
        return Status::failure(FailureCode::ContextUnavailable);
    if(header.count<0 || header.capacity<header.count || (header.count && !header.data))
        return Status::failure(FailureCode::CapturePreflightFailed);
    if(static_cast<std::size_t>(header.count)>output.entries.size())
        return Status::failure(FailureCode::UnsupportedContent);
    if(header.count && !CopyBytes(output.entries.data(),header.data,static_cast<std::size_t>(header.count)*16))
        return Status::failure(FailureCode::ContextUnavailable);
    output.count=static_cast<std::uint32_t>(header.count);
    return Status::success();
}

Status Sc6ReplayVfxState::ReadEventBindings(std::uintptr_t base,std::uintptr_t component,EventBindings& output) noexcept
{
    output={};
    __try {
        // Registered common captures have a cached world. Do not infer a null
        // event manager by skipping the component's native GetWorld fallback.
        output.world=At<std::uintptr_t>(reinterpret_cast<void*>(component),0x1c8);
        output.owner=At<std::uintptr_t>(reinterpret_cast<void*>(component),0x190);
        if(!output.world || !Bind(base,output.world_weak,output.world))
            return Status::failure(FailureCode::GenerationMismatch);
        output.manager=At<std::uintptr_t>(reinterpret_cast<void*>(output.world),0xc0);
        if(output.manager) {
            if(!Bind(base,output.manager_weak,output.manager))return Status::failure(FailureCode::GenerationMismatch);
            const auto table=At<std::uintptr_t>(reinterpret_cast<void*>(output.manager),0);
            if(table!=base+0x394b368)return Status::failure(FailureCode::UnsupportedContent);
            constexpr std::array<std::uintptr_t,4> routes{0x1f9ba80,0x1f9b840,0x1f9b5b0,0x1f9b3e0};
            for(std::size_t i=0;i<routes.size();++i)
                if(At<std::uintptr_t>(reinterpret_cast<void*>(table),0x5f8+i*8)!=base+routes[i])
                    return Status::failure(FailureCode::UnsupportedContent);
        }
        if(output.owner) {
            if(!Bind(base,output.owner_weak,output.owner))return Status::failure(FailureCode::GenerationMismatch);
            output.owner_class=At<std::uintptr_t>(reinterpret_cast<void*>(output.owner),0x10);
            const auto emitter_class=reinterpret_cast<std::uintptr_t(*)()>(base+0x234f470)();
            if(!emitter_class || !output.owner_class)return Status::failure(FailureCode::UnsupportedContent);
            // Same indexed ancestry predicate as all four native dispatchers;
            // an AEmitter subclass must not bypass its owner delegate routes.
            const auto depth=At<int>(reinterpret_cast<void*>(emitter_class),0x90);
            const auto owner_depth=At<int>(reinterpret_cast<void*>(output.owner_class),0x90);
            if(depth<0 || depth>256 || owner_depth<0 || owner_depth>256)
                return Status::failure(FailureCode::UnsupportedContent);
            if(depth<=owner_depth) {
                const auto ancestry=At<std::uintptr_t>(reinterpret_cast<void*>(output.owner_class),0x88);
                if(!ancestry)return Status::failure(FailureCode::UnsupportedContent);
                output.emitter_owner=At<std::uintptr_t>(reinterpret_cast<void*>(ancestry),std::size_t(depth)*8)==emitter_class+0x88;
            }
            if(output.emitter_owner)for(const auto offset:{0x398u,0x3a8u,0x3b8u,0x3c8u}) {
                const auto route=At<Header>(reinterpret_cast<void*>(output.owner),offset);
                if(route.count!=0 || route.capacity<0 || (route.capacity && !route.data))
                    return Status::failure(FailureCode::UnsupportedContent);
            }
        }
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayVfxState::CheckComponentBoundary(std::uintptr_t base, const ComponentBinding& component) noexcept
{
    __try
    {
        const auto* object = reinterpret_cast<const void*>(component.address);
        if (!Live(base, component.weak, component.address)
            || At<std::uintptr_t>(reinterpret_cast<void*>(At<std::uintptr_t>(object, 0)), 0x300) != component.tick_entry
            || At<std::uintptr_t>(object, 0x1d0) != component.attach_parent
            || At<std::uint64_t>(object, 0x238) != component.attach_socket
            || At<std::uintptr_t>(object, 0x808) != component.particle_template)
            return Status::failure(FailureCode::GenerationMismatch);
        if(component.lux) {
            std::array<MaterialRoots,2> roots{};
            if(!ReadMaterialRoots(base,component.address,roots) || roots!=component.material_roots)
                return Status::failure(FailureCode::GenerationMismatch);
        }
        CompletionBindings completions{};
        const auto completion_status=ReadCompletionBindings(component.address,completions);
        if(!completion_status.ok())return completion_status;
        if(completions!=component.completions)return Status::failure(FailureCode::GenerationMismatch);
        EventBindings events{};
        const auto event_status=ReadEventBindings(base,component.address,events);
        if(!event_status.ok())return event_status;
        if(events!=component.events)return Status::failure(FailureCode::GenerationMismatch);
        // Completion releases +A70 before callbacks, but only the enclosing
        // completed-application owner proves that those callbacks returned.
        // This participant never drains or copies the old task itself.
        if (At<std::uintptr_t>(object, 0xa70) || At<std::uint8_t>(object, 0xa48)
            || At<std::uint8_t>(object, 0xa80) || At<std::uint8_t>(object, 0xa81)
            || At<std::int32_t>(object, 0x3d8) != 0 || At<std::int32_t>(object, 0x960) != 0)
            return Status::failure(FailureCode::RestorePreflightFailed);
        if (!(At<std::uint32_t>(object, 0x188) & 1) || At<std::uint8_t>(object, 0x908) != 0)
            return Status::failure(FailureCode::UnsupportedContent);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ReadComponentValues(std::uintptr_t base, ComponentBinding& component) noexcept
{
    __try
    {
        const auto* object = reinterpret_cast<const void*>(component.address);
        component.tick_entry = At<std::uintptr_t>(reinterpret_cast<void*>(At<std::uintptr_t>(object, 0)), 0x300);
        component.lux = component.tick_entry == base + 0x8d8a10;
        if (!component.lux && component.tick_entry != base + 0x1f853c0)
            return Status::failure(FailureCode::UnsupportedContent);
        component.attach_parent = At<std::uintptr_t>(object, 0x1d0);
        component.attach_socket = At<std::uint64_t>(object, 0x238);
        component.particle_template = At<std::uintptr_t>(object, 0x808);
        component.active_flags = At<std::uint32_t>(object, 0x188) & 0x60000;
        if(component.lux && !ReadMaterialRoots(base,component.address,component.material_roots))
            return Status::failure(FailureCode::UnsupportedContent);
        const auto completions=ReadCompletionBindings(component.address,component.completions);
        if(!completions.ok())return completions;
        const auto events=ReadEventBindings(base,component.address,component.events);
        if(!events.ok())return events;
        const auto status = CheckComponentBoundary(base, component);
        if (!status.ok()) return status;
        std::size_t cursor = 0;
        for (const auto span : component_spans)
        {
            if (span.lux && !component.lux) continue;
            if (span.bytes > component.values.size() - cursor) return Status::failure(FailureCode::CapacityExceeded);
            std::memcpy(component.values.data() + cursor, static_cast<const std::byte*>(object) + span.offset, span.bytes);
            cursor += span.bytes;
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::CaptureComponent(std::uintptr_t component, std::size_t budget)
{
    if (!component) return Status::success();
    for (const auto& prior : components_)
        if (prior.address == component) return Live(base_, prior.weak, component)
            ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
    capture_rejection_ = {component, 0};
    ComponentBinding binding{component};
    if (!Bind(base_, binding.weak, component)) return Status::failure(FailureCode::GenerationMismatch);
    const auto used = owned_bytes();
    const auto replacement = (components_.size() + 1) * sizeof(ComponentBinding);
    if (used > budget || replacement > budget - used) return Status::failure(FailureCode::CapacityExceeded);
    components_.reserve(components_.size() + 1);
    components_.push_back(std::move(binding));
    const auto values = ReadComponentValues(base_, components_.back());
    if (!values.ok()) return values;
    std::uintptr_t fence{};
    std::uint8_t busy{};
    std::uint64_t event_flags{};
    if (!CopyBytes(&fence, reinterpret_cast<void*>(component + 0xa70), 8)
        || !CopyBytes(&busy, reinterpret_cast<void*>(component + 0xa81), 1)
        || (fence && !CopyBytes(&event_flags, reinterpret_cast<void*>(fence + 8), 8)))
        return Status::failure(FailureCode::ContextUnavailable);
    if (busy || (fence && !(event_flags & (1ull << 26))))
        return Status::failure(FailureCode::CapturePreflightFailed);
    Header emitters{};
    if (!CopyBytes(&emitters, reinterpret_cast<void*>(component + 0xa50), sizeof(emitters)))
        return Status::failure(FailureCode::ContextUnavailable);
    if (emitters.count < 0 || emitters.count > 128 || emitters.capacity < emitters.count || (emitters.count && !emitters.data))
        return Status::failure(FailureCode::CapturePreflightFailed);
    const auto component_used = owned_bytes();
    if (component_used > budget || static_cast<std::size_t>(emitters.count) > (budget - component_used) / sizeof(std::uintptr_t))
        return Status::failure(FailureCode::CapacityExceeded);
    auto& emitter_bindings = components_.back().emitters;
    emitter_bindings.resize(emitters.count);
    if (emitters.count && !CopyBytes(emitter_bindings.data(), emitters.data, emitters.count * sizeof(std::uintptr_t)))
        return Status::failure(FailureCode::ContextUnavailable);
    if (owned_bytes() > budget) return Status::failure(FailureCode::CapacityExceeded);
    for (int i = 0; i < emitters.count; ++i)
    {
        std::uintptr_t emitter{}, vtable{}, component_owner{}, system{}, system_vtable{}, address{};
        if (!CopyBytes(&emitter, emitters.data + static_cast<std::size_t>(i) * 8, 8))
            return Status::failure(FailureCode::ContextUnavailable);
        if (!emitter) continue;
        capture_rejection_.emitter = emitter;
        if (!CopyBytes(&vtable, reinterpret_cast<void*>(emitter), 8)
            || !CopyBytes(&component_owner, reinterpret_cast<void*>(emitter + 0x18), 8)
            || component_owner != component) return Status::failure(FailureCode::GenerationMismatch);
        if (Sc6ReplayCpuEmitterState::IsCpuVtable(base_, vtable))
        {
            const auto used = owned_bytes();
            const auto capacity = cpu_emitters_.size() + 1;
            if (used > budget || capacity > (budget - used) / sizeof(CpuEmitter))
                return Status::failure(FailureCode::CapacityExceeded);
            cpu_emitters_.reserve(capacity);
            cpu_emitters_.emplace_back();
            auto& image = cpu_emitters_.back();
            image.component = component; image.ordinal = static_cast<std::size_t>(i);
            const auto now = owned_bytes();
            if (now > budget) return Status::failure(FailureCode::CapacityExceeded);
            const auto status = image.image.Capture(base_, reinterpret_cast<void*>(component),
                reinterpret_cast<void*>(emitter), budget - now);
            if (!status.ok()) return status;
            continue;
        }
        if (vtable != base_ + 0x394c100) return Status::failure(FailureCode::UnsupportedContent);
        if (!CopyBytes(&system, reinterpret_cast<void*>(emitter + 0x1d0), 8) || !system
            || !CopyBytes(&system_vtable, reinterpret_cast<void*>(system), 8)
            || system_vtable != base_ + 0x3941830
            || !CopyBytes(&address, reinterpret_cast<void*>(system + 0x78), 8) || !address)
            return Status::failure(FailureCode::GenerationMismatch);
        std::uintptr_t tiles{}, render{}; int tile_count{}, tile_capacity{};
        if (!CopyBytes(&tiles, reinterpret_cast<void*>(emitter + 0x1e8), 8)
            || !CopyBytes(&tile_count, reinterpret_cast<void*>(emitter + 0x1f0), 4)
            || !CopyBytes(&tile_capacity, reinterpret_cast<void*>(emitter + 0x1f4), 4)
            || !CopyBytes(&render, reinterpret_cast<void*>(emitter + 0x1e0), 8) || !render
            || tile_count < 0 || tile_count > 65536 || tile_capacity < tile_count || (tile_count && !tiles))
            return Status::failure(FailureCode::GenerationMismatch);
        const auto owner_used = owned_bytes();
        const auto owner_growth = (gpu_owners_.size() + 1) * sizeof(GpuOwner);
        if (owner_used > budget || owner_growth > budget - owner_used
            || static_cast<std::size_t>(tile_count) > (budget - owner_used - owner_growth) / 4)
            return Status::failure(FailureCode::CapacityExceeded);
        gpu_owners_.reserve(gpu_owners_.size() + 1);
        gpu_owners_.emplace_back();
        auto& gpu = gpu_owners_.back();
        gpu.component = component; gpu.emitter = emitter; gpu.system = system;
        gpu.pool = address; gpu.render_storage = render; gpu.ordinal = static_cast<std::size_t>(i);
        if (!CopyBytes(&gpu.coordinate_resource, reinterpret_cast<void*>(render + 0x48), 8) || !gpu.coordinate_resource)
            return Status::failure(FailureCode::GenerationMismatch);
        gpu.tiles.resize(tile_count);
        if (tile_count && !CopyBytes(gpu.tiles.data(), reinterpret_cast<void*>(tiles), tile_count * 4))
            return Status::failure(FailureCode::ContextUnavailable);
        const auto captured_bytes = owned_bytes();
        if (captured_bytes > budget) return Status::failure(FailureCode::CapacityExceeded);
        const auto gpu_status = gpu.image.CaptureGpu(base_, reinterpret_cast<void*>(component),
            reinterpret_cast<void*>(emitter), budget - captured_bytes);
        if (!gpu_status.ok()) return gpu_status;
        bool present = false;
        for (const auto& pool : tile_pools_)
            if (pool.system == system)
            {
                if (pool.address != address) return Status::failure(FailureCode::GenerationMismatch);
                present = true; break;
            }
        if (present) continue;
        const auto used = owned_bytes();
        // Reserve explicitly so vector growth cannot silently double the
        // metadata allocation; old/new backing coexist during reserve.
        const auto replacement_bytes = tile_pools_.size() == tile_pools_.capacity()
            ? (tile_pools_.size() + 1) * sizeof(TilePool) : 0;
        if (used > budget || replacement_bytes > budget - used)
            return Status::failure(FailureCode::CapacityExceeded);
        if (replacement_bytes) tile_pools_.reserve(tile_pools_.size() + 1);
        tile_pools_.emplace_back();
        auto& pool = tile_pools_.back();
        pool.system = system; pool.address = address;
        std::size_t count{};
        auto status = ReadTilePool(address, nullptr, 0, count, false);
        if (!status.ok()) return status;
        const auto remaining_used = owned_bytes();
        if (remaining_used > budget || count > (budget - remaining_used) / 4)
            return Status::failure(FailureCode::CapacityExceeded);
        pool.free_indices = std::make_unique<std::uint32_t[]>(count);
        pool.count = count;
        status = ReadTilePool(address, pool.free_indices.get(), count, count, false);
        if (!status.ok()) return status;
    }
    return Status::success();
}

Status Sc6ReplayVfxState::RetainOwners(std::size_t budget, std::span<void* const> companion_owners)
{
    // Audited UObject bindings only. GPU +1D8 is an interior pointer into
    // TypeData: retain its verified UObject owner, never the descriptor itself.
    // +1E0 is separately owned native render storage, not a GC object.
    std::array<void*, 4096> objects{};
    const auto used = owned_bytes();
    if (used > budget || sizeof(objects) > budget - used)
        return Status::failure(FailureCode::CapacityExceeded);
    std::size_t count{};
    const auto add = [&](std::uintptr_t address) {
        if (!address) return true;
        auto* object = reinterpret_cast<void*>(address);
        for (std::size_t i = 0; i < count; ++i) if (objects[i] == object) return true;
        if (count == objects.size()) return false;
        objects[count++] = object;
        return true;
    };
    if (!add(manager_)) return Status::failure(FailureCode::CapacityExceeded);
    for (auto* owner : companion_owners)
        if (!owner || !add(reinterpret_cast<std::uintptr_t>(owner))) return Status::failure(FailureCode::CapacityExceeded);
    for (const auto& component : components_)
    {
        if (!Live(base_, component.weak, component.address)) return Status::failure(FailureCode::GenerationMismatch);
        std::uintptr_t particle_template{};
        if (!CopyBytes(&particle_template, reinterpret_cast<void*>(component.address + 0x808), 8))
            return Status::failure(FailureCode::ContextUnavailable);
        if (particle_template != component.particle_template)
            return Status::failure(FailureCode::GenerationMismatch);
        if (!add(component.address) || !add(particle_template) || !add(component.attach_parent))
            return Status::failure(FailureCode::CapacityExceeded);
        if(!add(component.events.world) || !add(component.events.owner) || !add(component.events.manager))
            return Status::failure(FailureCode::CapacityExceeded);
        // 141F6FF20 dispatches these weak-object/FName bindings before native
        // slot destruction. Keep their owners in the existing GC lease; this
        // does not capture or replace arbitrary listener-owned mutable state.
        for(std::size_t i=0;i<component.completions.count;++i) {
            const auto object=reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(component.completions.entries[i].data());
            if(!object)return Status::failure(FailureCode::GenerationMismatch);
            if(!add(object))return Status::failure(FailureCode::CapacityExceeded);
        }
        for(const auto& roots:component.material_roots)
            for(int i=0;i<roots.count;++i)
                if(!add(roots.entries[i].object) || !add(roots.entries[i].parent))
                    return Status::failure(FailureCode::CapacityExceeded);
        for (const auto emitter : component.emitters)
        {
            if (!emitter) continue;
            for (const auto offset : {0x10, 0x28, 0x1a0})
            {
                std::uintptr_t object{};
                if (!CopyBytes(&object, reinterpret_cast<void*>(emitter + offset), 8))
                    return Status::failure(FailureCode::ContextUnavailable);
                if (!add(object)) return Status::failure(FailureCode::CapacityExceeded);
            }
        }
    }
    for(const auto& emitter:cpu_emitters_)
        if(!add(emitter.image.event_module()) || !add(emitter.image.second_instance_module())
            || !add(emitter.image.emitter_type_data())
            || !add(emitter.image.mesh_asset())) return Status::failure(FailureCode::CapacityExceeded);
    for(const auto& emitter:gpu_owners_)
        if(!add(emitter.image.event_module()) || !add(emitter.image.second_instance_module())
            || !add(emitter.image.emitter_type_data()) || !add(emitter.image.mesh_asset())
            || !add(emitter.image.vector_field_asset())) return Status::failure(FailureCode::CapacityExceeded);
    object_lease_ = std::make_unique<Sc6ReplayObjectLease>();
    auto status=object_lease_->Acquire(base_, {objects.data(), count}, budget - used - sizeof(objects));
    if(!status.ok())return status;
    // Build the second lease while every captured component is still live.
    // Never filter an invalidated lease after destruction: collector changes
    // and explicit death must not turn into permission to ignore other owners.
    std::size_t dependencies{};
    for(std::size_t i=0;i<count;++i) {
        const auto address=reinterpret_cast<std::uintptr_t>(objects[i]);
        const bool component=std::any_of(components_.begin(),components_.end(),
            [&](const auto& row){return row.address==address;});
        if(!component)objects[dependencies++]=objects[i];
    }
    const auto retained_bytes=owned_bytes();
    if(retained_bytes>budget || sizeof(objects)>budget-retained_bytes)
        return Status::failure(FailureCode::CapacityExceeded);
    reconstruction_lease_=std::make_unique<Sc6ReplayObjectLease>();
    return reconstruction_lease_->Acquire(base_,{objects.data(),dependencies},budget-retained_bytes-sizeof(objects));
}

Status Sc6ReplayVfxState::ReleaseOwners() noexcept
{
    auto status=Status::success();
    if(object_lease_) {
        status=object_lease_->Release();
        if(!status.ok())return status;
        object_lease_.reset();valid_=false;
    }
    if(reconstruction_lease_) {
        status=reconstruction_lease_->Release();
        if(status.ok()){reconstruction_lease_.reset();valid_=false;}
    }
    return status;
}

Status Sc6ReplayVfxState::CaptureTilePools(std::size_t budget, std::span<void* const> candidates)
{
    for (int i = 0; i < counts_[0]; ++i)
    {
        const auto status = CaptureComponent(At<std::uintptr_t>(slots_[i].bytes.data(), 0), budget);
        if (!status.ok()) return status;
    }
    // The caller supplies registered components from this replay world. CPU-only
    // stage emitters also own evolving state even though they have no shared GPU
    // pool. Capturing just the pool's consumers omitted those native updates.
    for(void* candidate : candidates) {
        if(!candidate) return Status::failure(FailureCode::ContextUnavailable);
        const auto status=CaptureComponent(reinterpret_cast<std::uintptr_t>(candidate),budget);
        if(!status.ok()) return status;
    }
    return ValidateOwnerPartition();
}

Status Sc6ReplayVfxState::ValidateOwnerPartition() const noexcept
{
    for (const auto& pool : tile_pools_)
    {
        std::array<std::uint64_t, 1024> owned{};
        for (const auto& component : components_)
        {
            if (!Live(base_, component.weak, component.address)) return Status::failure(FailureCode::GenerationMismatch);
            Header emitters{};
            std::uintptr_t fence{}; std::uint8_t busy{}; std::uint64_t flags{};
            if (!CopyBytes(&fence, reinterpret_cast<void*>(component.address + 0xa70), 8)
                || !CopyBytes(&busy, reinterpret_cast<void*>(component.address + 0xa81), 1)
                || (fence && !CopyBytes(&flags, reinterpret_cast<void*>(fence + 8), 8))
                || !CopyBytes(&emitters, reinterpret_cast<void*>(component.address + 0xa50), sizeof(emitters)))
                return Status::failure(FailureCode::ContextUnavailable);
            if (busy || (fence && !(flags & (1ull << 26))) || emitters.count < 0 || emitters.count > 128
                || emitters.capacity < emitters.count || (emitters.count && !emitters.data))
                return Status::failure(FailureCode::CapturePreflightFailed);
            if (static_cast<std::size_t>(emitters.count) != component.emitters.size())
                return Status::failure(FailureCode::GenerationMismatch);
            for (int i = 0; i < emitters.count; ++i)
            {
                std::uintptr_t emitter{}, vt{}, owner{}, system{}, address{}, tiles{}; int count{}, capacity{};
                if (!CopyBytes(&emitter, emitters.data + i * 8, 8)) return Status::failure(FailureCode::ContextUnavailable);
                if (emitter != component.emitters[i]) return Status::failure(FailureCode::GenerationMismatch);
                if (!emitter) continue;
                if (!CopyBytes(&vt, reinterpret_cast<void*>(emitter), 8)
                    || !CopyBytes(&owner, reinterpret_cast<void*>(emitter + 0x18), 8) || owner != component.address)
                    return Status::failure(FailureCode::GenerationMismatch);
                if (Sc6ReplayCpuEmitterState::IsCpuVtable(base_, vt)) continue;
                if (vt != base_ + 0x394c100) return Status::failure(FailureCode::UnsupportedContent);
                if (!CopyBytes(&system, reinterpret_cast<void*>(emitter + 0x1d0), 8) || !system
                    || !CopyBytes(&address, reinterpret_cast<void*>(system + 0x78), 8))
                    return Status::failure(FailureCode::ContextUnavailable);
                const GpuOwner* captured = nullptr;
                for (const auto& candidate : gpu_owners_)
                    if (candidate.component == component.address && candidate.ordinal == static_cast<std::size_t>(i))
                    { captured = &candidate; break; }
                if (!captured || captured->emitter != emitter || captured->system != system || captured->pool != address)
                    return Status::failure(FailureCode::GenerationMismatch);
                if (address != pool.address) continue;
                if (system != pool.system || !CopyBytes(&tiles, reinterpret_cast<void*>(emitter + 0x1e8), 8)
                    || !CopyBytes(&count, reinterpret_cast<void*>(emitter + 0x1f0), 4)
                    || !CopyBytes(&capacity, reinterpret_cast<void*>(emitter + 0x1f4), 4)
                    || count < 0 || count > 65536 || capacity < count || (count && !tiles))
                    return Status::failure(FailureCode::GenerationMismatch);
                std::uintptr_t render{};
                if (!captured || captured->emitter != emitter || captured->system != system || captured->pool != address
                    || captured->tiles.size() != static_cast<std::size_t>(count)
                    || !CopyBytes(&render, reinterpret_cast<void*>(emitter + 0x1e0), 8) || render != captured->render_storage)
                    return Status::failure(FailureCode::GenerationMismatch);
                for (int tile_index = 0; tile_index < count; ++tile_index)
                {
                    std::uint32_t tile{};
                    if (!CopyBytes(&tile, reinterpret_cast<void*>(tiles + tile_index * 4), 4) || tile >= 65536)
                        return Status::failure(FailureCode::GenerationMismatch);
                    const auto mask = std::uint64_t{1} << (tile % 64);
                    if (tile != captured->tiles[tile_index] || (owned[tile / 64] & mask)) return Status::failure(FailureCode::GenerationMismatch);
                    owned[tile / 64] |= mask;
                }
            }
        }
        if (!IsCompleteTilePartition(owned, {pool.free_indices.get(), pool.count}))
            return Status::failure(FailureCode::GenerationMismatch);
        std::size_t free_count{};
        auto status = ValidateNativeTilePartition(pool.address, owned, free_count);
        if (!status.ok() || free_count != pool.count) return Status::failure(FailureCode::GenerationMismatch);
    }
    return Status::success();
}

bool Sc6ReplayVfxState::SameTilePools() const noexcept
{
    for (const auto& pool : tile_pools_)
    {
        std::uintptr_t vtable{}, address{};
        if (!CopyBytes(&vtable, reinterpret_cast<void*>(pool.system), 8) || vtable != base_ + 0x3941830
            || !CopyBytes(&address, reinterpret_cast<void*>(pool.system + 0x78), 8) || address != pool.address) return false;
        std::size_t count{};
        const auto status = ReadTilePool(address, pool.free_indices.get(), pool.count, count, true);
        if (!status.ok() || count != pool.count) return false;
    }
    return true;
}

std::size_t Sc6ReplayVfxState::PreparedTilePools::owned_bytes() const noexcept
{
    auto bytes = sizeof(*this) + patches_.capacity() * sizeof(Patch)
        + reconstruction_bindings_.capacity() * sizeof(ReconstructionBinding);
    for (const auto& patch : patches_) bytes += (patch.target.capacity() + patch.previous.capacity()) * sizeof(std::uint32_t);
    return bytes;
}

bool Sc6ReplayVfxState::PreparedTilePools::published() const noexcept
{
    // Individual pool writes can have been undone while final B verification
    // still fails. Keep the whole transaction dirty until all pools verify.
    return in_flight_;
}

Status Sc6ReplayVfxState::PreparedTilePools::CheckPool(const Patch& patch, bool target) const noexcept
{
    __try
    {
        if (At<std::uintptr_t>(reinterpret_cast<void*>(patch.system), 0) != base_ + 0x3941830
            || At<std::uintptr_t>(reinterpret_cast<void*>(patch.system), 0x78) != patch.pool
            || At<std::uintptr_t>(reinterpret_cast<void*>(patch.pool), 0) != base_ + 0x394bd48)
            return Status::failure(FailureCode::GenerationMismatch);
        const auto& image = target ? patch.target : patch.previous;
        auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(patch.pool + 0x40198);
        if (!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::RestorePreflightFailed);
        __try
        {
            if (At<int>(reinterpret_cast<void*>(patch.pool), 0x40190) != static_cast<int>(image.size())
                || (!image.empty() && std::memcmp(reinterpret_cast<void*>(patch.pool + 0x190), image.data(), image.size() * 4)))
                return Status::failure(FailureCode::GenerationMismatch);
            return Status::success();
        }
        __finally { LeaveCriticalSection(lock); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ReplaceNativeTilePrefix(std::uintptr_t pool, std::span<const std::uint32_t> expected,
    std::span<const std::uint32_t> replacement, bool compare_expected, bool& dirty) noexcept
{
    if (!pool || pool > UINTPTR_MAX - (0x40198 + sizeof(CRITICAL_SECTION))
        || expected.size() > 65536 || replacement.size() > 65536)
        return Status::failure(FailureCode::RestorePreflightFailed);
    if (!compare_expected && !dirty) return Status::failure(FailureCode::IllegalTransition);
    __try
    {
        auto* lock = reinterpret_cast<LPCRITICAL_SECTION>(pool + 0x40198);
        if (!TryEnterCriticalSection(lock)) return Status::failure(FailureCode::RestorePreflightFailed);
        __try
        {
            if (compare_expected && (At<int>(reinterpret_cast<void*>(pool), 0x40190) != static_cast<int>(expected.size())
                || (!expected.empty() && std::memcmp(reinterpret_cast<void*>(pool + 0x190), expected.data(), expected.size() * 4))))
                return Status::failure(FailureCode::GenerationMismatch);
            dirty = true; // A fault in memcpy may already have changed a prefix.
            if (!replacement.empty()) std::memcpy(reinterpret_cast<void*>(pool + 0x190), replacement.data(), replacement.size() * 4);
            *reinterpret_cast<int*>(pool + 0x40190) = static_cast<int>(replacement.size());
            if (At<int>(reinterpret_cast<void*>(pool), 0x40190) != static_cast<int>(replacement.size())
                || (!replacement.empty() && std::memcmp(reinterpret_cast<void*>(pool + 0x190), replacement.data(), replacement.size() * 4)))
                return Status::failure(FailureCode::RestoreVerificationFailed);
            return Status::success();
        }
        __finally { LeaveCriticalSection(lock); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreWriteFailed); }
}

Status Sc6ReplayVfxState::PreparedTilePools::WritePool(Patch& patch, bool undo) noexcept
{
    __try
    {
        if (At<std::uintptr_t>(reinterpret_cast<void*>(patch.system), 0) != base_ + 0x3941830
            || At<std::uintptr_t>(reinterpret_cast<void*>(patch.system), 0x78) != patch.pool
            || At<std::uintptr_t>(reinterpret_cast<void*>(patch.pool), 0) != base_ + 0x394bd48)
            return Status::failure(FailureCode::GenerationMismatch);
        const auto status = ReplaceNativeTilePrefix(patch.pool, patch.previous,
            undo ? patch.previous : patch.target, !undo, patch.dirty);
        if (patch.dirty) in_flight_ = true;
        if (status.ok() && undo) patch.dirty = false;
        return status;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreWriteFailed); }
}

Status Sc6ReplayVfxState::ValidatePoolTransaction(const Sc6ReplayVfxState& current, const PreparedTilePools& prepared) const noexcept
{
    if (prepared.executing_ && !prepared.execution_settled_) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || !current.valid_ || !prepared.ready_ || !object_lease_ || !current.object_lease_
        || prepared.base_ != base_ || current.base_ != base_ || prepared.manager_ != manager_ || current.manager_ != manager_
        || prepared.thread_ != GetCurrentThreadId() || thread_ != prepared.thread_ || current.thread_ != prepared.thread_)
        return Status::failure(FailureCode::GenerationMismatch);
    std::uint64_t epoch{};
    if (!CopyBytes(&epoch, reinterpret_cast<void*>(base_ + 0x4197170), sizeof(epoch)) || epoch != prepared.epoch_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = prepared.reconstruction_bindings_.empty() ? object_lease_->Validate()
        : prepared.executing_ ? ValidateReconstructionDependencies()
        : ValidateReconstructionBindings(prepared.reconstruction_bindings_);
    return status.ok() ? current.object_lease_->Validate() : status;
}

Status Sc6ReplayVfxState::PrepareTilePools(const Sc6ReplayVfxState& current, std::size_t budget, PreparedTilePools& output,
    std::span<const ReconstructionBinding> bindings) const noexcept
{
    if (output.ready_ || !output.patches_.empty() || tile_pools_.empty()) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || !current.valid_ || !object_lease_ || !current.object_lease_
        || base_ != current.base_ || manager_ != current.manager_
        || thread_ != GetCurrentThreadId() || current.thread_ != thread_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = bindings.empty() ? object_lease_->Validate() : ValidateReconstructionBindings(bindings);
    if (status.ok()) status = current.object_lease_->Validate();
    if (status.ok() && !current.SameTilePools()) status = Status::failure(FailureCode::GenerationMismatch);
    if (status.ok()) status = current.ValidateOwnerPartition();
    if (!status.ok()) return status;
    if (tile_pools_.size() != current.tile_pools_.size()) return Status::failure(FailureCode::GenerationMismatch);
    auto remaining = budget;
    const auto charge = [&remaining](std::size_t bytes) {
        if (bytes > remaining) return false;
        remaining -= bytes; return true;
    };
    if (!charge(owned_bytes()) || (this != &current && !charge(current.owned_bytes()))
        || !charge(output.owned_bytes()) || !charge(sizeof(PreparedTilePools))
        || tile_pools_.size() > remaining / sizeof(PreparedTilePools::Patch))
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        PreparedTilePools next;
        if (bindings.size()>remaining/sizeof(ReconstructionBinding))
            return Status::failure(FailureCode::CapacityExceeded);
        next.reconstruction_bindings_.assign(bindings.begin(),bindings.end());
        if (next.owned_bytes()-sizeof(next)>remaining) return Status::failure(FailureCode::CapacityExceeded);
        next.base_ = base_; next.manager_ = manager_; next.thread_ = thread_;
        if (!CopyBytes(&next.epoch_, reinterpret_cast<void*>(base_ + 0x4197170), sizeof(next.epoch_)))
            return Status::failure(FailureCode::ContextUnavailable);
        // remaining excludes the fixed next object, but includes its variable storage.
        next.patches_.reserve(tile_pools_.size());
        for (const auto& pool : tile_pools_)
        {
            const TilePool* live{};
            for (const auto& candidate : current.tile_pools_)
                if (candidate.system == pool.system && candidate.address == pool.address) { live = &candidate; break; }
            if (!live || pool.count > 65536 || live->count > 65536) return Status::failure(FailureCode::GenerationMismatch);
            const auto variable = next.owned_bytes() - sizeof(next);
            const auto requested = (pool.count + live->count) * sizeof(std::uint32_t);
            if (variable > remaining || requested > remaining - variable) return Status::failure(FailureCode::CapacityExceeded);
            next.patches_.emplace_back();
            auto& patch = next.patches_.back();
            patch.system = pool.system; patch.pool = pool.address;
            patch.target.resize(pool.count); patch.previous.resize(live->count);
            if (next.owned_bytes() - sizeof(next) > remaining) return Status::failure(FailureCode::CapacityExceeded);
            if (pool.count) std::memcpy(patch.target.data(), pool.free_indices.get(), pool.count * 4);
            if (live->count) std::memcpy(patch.previous.data(), live->free_indices.get(), live->count * 4);
            for (const auto& owner : gpu_owners_) if (owner.pool == pool.address)
                for (const auto tile : owner.tiles)
                {
                    if (tile >= 65536) return Status::failure(FailureCode::GenerationMismatch);
                    const auto bit = std::uint64_t{1} << (tile % 64);
                    if (patch.target_owned[tile / 64] & bit) return Status::failure(FailureCode::GenerationMismatch);
                    patch.target_owned[tile / 64] |= bit;
                }
            if (!IsCompleteTilePartition(patch.target_owned, patch.target)) return Status::failure(FailureCode::GenerationMismatch);
            status = next.CheckPool(patch, false);
            if (!status.ok()) return status;
        }
        next.ready_ = true;
        status = ValidatePoolTransaction(current, next);
        if (!status.ok()) return status;
        output.patches_.swap(next.patches_);
        output.reconstruction_bindings_.swap(next.reconstruction_bindings_);
        output.base_ = next.base_; output.manager_ = next.manager_; output.thread_ = next.thread_;
        output.epoch_ = next.epoch_; output.ready_ = true;
        return Status::success();
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplayVfxState::PublishTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept
{
    if (prepared.published()) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidatePoolTransaction(current, prepared);
    if (!status.ok()) return status;
    for (const auto& patch : prepared.patches_)
    {
        status = prepared.CheckPool(patch, false);
        if (!status.ok()) return status;
    }
    for (auto& patch : prepared.patches_)
    {
        status = prepared.WritePool(patch, false);
        if (!status.ok()) break;
    }
    if (status.ok()) status = ValidatePublishedTilePools(current, prepared);
    if (status.ok()) return status;
    return UndoTilePools(current, prepared).ok() ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayVfxState::ValidatePublishedTilePools(const Sc6ReplayVfxState& current, const PreparedTilePools& prepared) const noexcept
{
    auto status = ValidatePoolTransaction(current, prepared);
    if (!status.ok()) return status;
    for (const auto& patch : prepared.patches_)
    {
        if (!patch.dirty) return Status::failure(FailureCode::IllegalTransition);
        status = prepared.CheckPool(patch, true);
        if (!status.ok()) return status;
        std::size_t count{};
        status = ValidateNativeTilePartition(patch.pool, patch.target_owned, count);
        if (!status.ok() || count != patch.target.size()) return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    return Status::success();
}

Status Sc6ReplayVfxState::UndoTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept
{
    if (!prepared.published()) return Status::success();
    auto status = ValidatePoolTransaction(current, prepared);
    if (!status.ok()) return Status::failure(FailureCode::UndoFailed);
    for (auto i = prepared.patches_.size(); i > 0; --i)
    {
        auto& patch = prepared.patches_[i - 1];
        if (patch.dirty && !prepared.WritePool(patch, true).ok()) return Status::failure(FailureCode::UndoFailed);
    }
    for (const auto& patch : prepared.patches_)
        if (!prepared.CheckPool(patch, false).ok()) return Status::failure(FailureCode::UndoFailed);
    prepared.in_flight_ = false;
    return Status::success();
}

Status Sc6ReplayVfxState::CommitTilePools(const Sc6ReplayVfxState& current, PreparedTilePools& prepared) const noexcept
{
    const auto status = ValidatePublishedTilePools(current, prepared);
    if (!status.ok()) return status;
    // Native pool storage is inline: only private A/B images retire here. The
    // enclosing transaction must have verified every active emitter owner too.
    std::vector<PreparedTilePools::Patch>().swap(prepared.patches_);
    std::vector<ReconstructionBinding>().swap(prepared.reconstruction_bindings_);
    prepared.ready_ = false;
    prepared.in_flight_ = false;
    prepared.executing_ = prepared.execution_settled_ = false;
    return Status::success();
}

Status Sc6ReplayVfxState::BeginTilePoolExecution(const Sc6ReplayVfxState& current,
    PreparedTilePools& prepared, std::size_t budget) const noexcept
{
    if (prepared.executing_) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidatePublishedTilePools(current, prepared);
    if (!status.ok()) return status;
    // Pools have fixed inline native storage. Reserve the maximum C prefix
    // before execution so even a full pool can be settled without allocating.
    try {
        for (auto& patch : prepared.patches_) {
            const auto used = prepared.owned_bytes();
            if (used > budget || 65536 * sizeof(std::uint32_t) > budget - used)
                return Status::failure(FailureCode::CapacityExceeded);
            patch.target.reserve(65536);
            if (prepared.owned_bytes() > budget) return Status::failure(FailureCode::CapacityExceeded);
        }
    } catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    prepared.executing_ = true;
    return Status::success();
}

Status Sc6ReplayVfxState::SettleTilePoolExecution(const Sc6ReplayVfxState& current,
    const Sc6ReplayVfxState& observed, PreparedTilePools& prepared) const noexcept
{
    if (!prepared.executing_ || prepared.execution_settled_ || !prepared.in_flight_ || !prepared.ready_)
        return Status::failure(FailureCode::IllegalTransition);
    if (!(prepared.reconstruction_bindings_.empty() ? retained_owners_live() : ValidateReconstructionDependencies().ok())
        || !current.retained_owners_live() || !observed.retained_owners_live()
        || prepared.thread_ != GetCurrentThreadId() || prepared.base_ != base_ || current.base_ != base_
        || observed.base_ != base_ || prepared.manager_ != manager_ || observed.manager_ != manager_
        || current.manager_ != manager_ || observed.tile_pools_.size() != prepared.patches_.size())
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = observed.ValidateOwnerPartition();
    if (!status.ok() || !observed.SameTilePools()) return Status::failure(FailureCode::GenerationMismatch);
    std::uint64_t epoch{};
    if (!CopyBytes(&epoch, reinterpret_cast<void*>(base_ + 0x4197170), sizeof(epoch)))
        return Status::failure(FailureCode::ContextUnavailable);
    // Validate every identity and extent before changing the expected C
    // prefix. B's private prefix is never changed by native pool allocation.
    for (const auto& patch : prepared.patches_) {
        const TilePool* found = nullptr;
        for (const auto& pool : observed.tile_pools_)
            if (pool.system == patch.system && pool.address == patch.pool) { found = &pool; break; }
        if (!patch.dirty || !found || found->count > patch.target.capacity() || found->count > 65536)
            return Status::failure(FailureCode::GenerationMismatch);
    }
    for (auto& patch : prepared.patches_) {
        for (const auto& pool : observed.tile_pools_)
            if (pool.system == patch.system && pool.address == patch.pool) {
                patch.target.resize(pool.count);
                if (pool.count) std::memcpy(patch.target.data(), pool.free_indices.get(), pool.count * sizeof(std::uint32_t));
                break;
            }
        patch.target_owned = {};
        for (const auto& owner : observed.gpu_owners_) if (owner.pool == patch.pool)
            for (const auto tile : owner.tiles) patch.target_owned[tile / 64] |= std::uint64_t{1} << (tile % 64);
    }
    // This records admission at the physical C epoch; it never rewinds the
    // engine clock. The enclosing owner has already drained C render work.
    prepared.epoch_ = epoch;
    prepared.execution_settled_ = true;
    return ValidatePublishedTilePools(current, prepared);
}

Status Sc6ReplayVfxState::ReopenTilePoolsForUndo(const Sc6ReplayVfxState& original, PreparedTilePools& prepared) const noexcept
{
    if(!prepared.executing_ || !prepared.in_flight_ || !prepared.ready_)
        return Status::failure(FailureCode::IllegalTransition);
    if(!prepared.execution_settled_)return Status::success();
    auto status=ValidatePublishedTilePools(original,prepared);if(!status.ok())return status;
    // Fixed inline storage remains native-owned; retain both copied prefixes
    // until recovery observes the pool after C-only emitter retirement.
    prepared.execution_settled_=false;return Status::success();
}

Status Sc6ReplayVfxState::CaptureDefinitionMap(std::size_t budget)
{
    const auto address = manager_ + 0x408;
    if (!CopyBytes(definition_layout_.data(), reinterpret_cast<void*>(address), definition_layout_.size()))
        return Status::failure(FailureCode::ContextUnavailable);
    const auto* h = definition_layout_.data();
    const auto slots = At<int>(h, 8), capacity = At<int>(h, 0xc);
    const auto bits = At<int>(h, 0x28), max_bits = At<int>(h, 0x2c);
    const auto free = At<int>(h, 0x34), hashes = At<int>(h, 0x48);
    const auto flags = At<std::uintptr_t>(h, 0x20), hash = At<std::uintptr_t>(h, 0x40);
    if (slots < 0 || capacity < slots || bits != slots || max_bits < bits
        || free < 0 || free > slots || hashes < 0
        || (hashes && (hashes & (hashes - 1))) || (!flags && max_bits > 128) || (!hash && hashes > 2))
        return Status::failure(FailureCode::CapturePreflightFailed);
    const std::array<std::size_t, 3> counts{static_cast<std::size_t>(slots),
        (static_cast<std::size_t>(bits) + 31) / 32, static_cast<std::size_t>(hashes)};
    const std::array<std::uintptr_t, 3> sources{At<std::uintptr_t>(h, 0),
        flags ? flags : address + 0x10, hash ? hash : address + 0x38};
    for (std::size_t i = 0; i < counts.size(); ++i)
    {
        const auto bytes = counts[i] * definition_strides[i];
        const auto used = owned_bytes();
        if (used > budget || bytes > budget - used) return Status::failure(FailureCode::CapacityExceeded);
        if (!bytes) continue;
        auto& storage = definition_storage_[i];
        storage.bytes = std::make_unique<std::byte[]>(bytes);
        storage.count = counts[i];
        if (!sources[i] || !CopyBytes(storage.bytes.get(), reinterpret_cast<void*>(sources[i]), bytes))
            return Status::failure(FailureCode::ContextUnavailable);
    }
    const auto occupied = [&](int index) {
        return (At<std::uint32_t>(definition_storage_[1].bytes.get(), (index / 32) * 4) >> (index % 32)) & 1u;
    };
    int live = 0;
    for (int i = 0; i < slots; ++i) live += occupied(i);
    if (live != slots - free) return Status::failure(FailureCode::CapturePreflightFailed);
    // Native 1408A1380 rebuilds chains in sparse-slot order. Preserve that
    // ordering, including a not-yet-created hash table, instead of reinserting
    // key/value pairs in a potentially different order.
    int visited = 0;
    for (int bucket = 0; bucket < hashes; ++bucket)
        for (int index = At<int>(definition_storage_[2].bytes.get(), bucket * 4); index != -1;)
        {
            if (index < 0 || index >= slots || !occupied(index) || ++visited > live)
                return Status::failure(FailureCode::CapturePreflightFailed);
            const auto* node = definition_storage_[0].bytes.get() + index * 32;
            const auto expected = ((At<unsigned char>(node, 0) << 8) | At<unsigned char>(node, 4)) & (hashes - 1);
            if (At<int>(node, 0x1c) != bucket || expected != bucket)
                return Status::failure(FailureCode::CapturePreflightFailed);
            index = At<int>(node, 0x18);
        }
    return hashes && visited != live ? Status::failure(FailureCode::CapturePreflightFailed) : Status::success();
}

bool Sc6ReplayVfxState::SameDefinitionMap() const noexcept
{
    __try
    {
        const auto address = manager_ + 0x408;
        // This is validation while held, not a historical binding comparison.
        // Even a reallocation/rehash must not occur during this hold.
        if (std::memcmp(reinterpret_cast<void*>(address), definition_layout_.data(), definition_layout_.size())) return false;
        const auto* h = definition_layout_.data();
        const auto flags = At<std::uintptr_t>(h, 0x20), hash = At<std::uintptr_t>(h, 0x40);
        const std::array<std::uintptr_t, 3> sources{At<std::uintptr_t>(h, 0),
            flags ? flags : address + 0x10, hash ? hash : address + 0x38};
        for (std::size_t i = 0; i < sources.size(); ++i)
            if (definition_storage_[i].count && std::memcmp(reinterpret_cast<void*>(sources[i]),
                definition_storage_[i].bytes.get(), definition_storage_[i].count * definition_strides[i])) return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayVfxState::CaptureTables(std::size_t budget)
{
    for (std::size_t i = 0; i < tables_.size(); ++i)
    {
        Header header{};
        if (!CopyBytes(&header, reinterpret_cast<void*>(manager_ + table_offsets[i]), sizeof(header)))
            return Status::failure(FailureCode::ContextUnavailable);
        if (header.count < 0 || header.count > 4096 || header.capacity < header.count || (header.count && !header.data))
            return Status::failure(FailureCode::CapturePreflightFailed);
        const auto bytes = static_cast<std::size_t>(header.count) * table_strides[i];
        if (owned_bytes() > budget || bytes > budget - owned_bytes())
            return Status::failure(FailureCode::CapacityExceeded);
        auto& table = tables_[i];
        table.bytes = std::make_unique<std::byte[]>(bytes);
        table.count = header.count;
        table.native_capacity = header.capacity;
        if (bytes && !CopyBytes(table.bytes.get(), header.data, bytes))
            return Status::failure(FailureCode::ContextUnavailable);
        if (i == 1)
            for (std::size_t row = 0; row < table.count; ++row)
                std::memset(table.bytes.get() + row * 12 + 1, 0, 3);
    }
    return Status::success();
}

bool Sc6ReplayVfxState::SameTables() const noexcept
{
    __try
    {
        for (std::size_t i = 0; i < tables_.size(); ++i)
        {
            const auto header = At<Header>(reinterpret_cast<void*>(manager_), table_offsets[i]);
            const auto& table = tables_[i];
            if (header.count < 0 || static_cast<std::size_t>(header.count) != table.count
                || header.capacity < header.count || (header.count && !header.data)) return false;
            if (i == 1)
            {
                for (std::size_t row = 0; row < table.count; ++row)
                    if (header.data[row * 12] != table.bytes[row * 12]
                        || std::memcmp(header.data + row * 12 + 4, table.bytes.get() + row * 12 + 4, 8)) return false;
            }
            else if (table.count && std::memcmp(header.data, table.bytes.get(), table.count * table_strides[i])) return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayVfxState::ReadManager(void* battle, std::uintptr_t& manager,
    std::int32_t& sequence, std::array<Header, 2>& arrays) noexcept
{
    __try
    {
        manager = At<std::uintptr_t>(battle, 0x508); // Native GetLuxVFxInstanceManager.
        if (!manager) return Status::failure(FailureCode::ContextUnavailable);
        sequence = At<std::int32_t>(reinterpret_cast<void*>(manager), 0x3e0);
        for (std::size_t i = 0; i < arrays.size(); ++i)
        {
            arrays[i] = At<Header>(reinterpret_cast<void*>(manager), 0x3e8 + i * 0x10);
            if (arrays[i].count < 0 || arrays[i].count > 4096 || arrays[i].capacity < arrays[i].count
                || (arrays[i].count && !arrays[i].data)) return Status::failure(FailureCode::CapturePreflightFailed);
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ValidateEmptyNormalizer() const noexcept
{
    __try
    {
        // 1408A2660 tests +3D0 before resolving the inline/heap callback or
        // invoking validity/normalization. Count zero has no callback payload
        // to retain. Nonempty payload and callback-owner state need an audit;
        // copying the type-erased pointer would not capture that state.
        const auto count = At<int>(reinterpret_cast<void*>(manager_), 0x3d0);
        if (count < 0) return Status::failure(FailureCode::CapturePreflightFailed);
        return count == 0 ? Status::success() : Status::failure(FailureCode::UnsupportedContent);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ValidateProvider(std::uintptr_t base, const std::byte* request,
    std::size_t& bytes) noexcept
{
    __try
    {
        const auto count = At<std::int32_t>(request, 0xa0);
        const auto* provider = Provider(request);
        if (!count) return provider ? Status::failure(FailureCode::UnsupportedContent) : Status::success();
        if (count != 3 || !provider) return Status::failure(FailureCode::UnsupportedContent);
        const auto vtable = At<std::uintptr_t>(provider, 0);
        const auto callback = At<std::uintptr_t>(provider, 0x10);
        if (!((vtable == base + 0x326c0c8 && callback == base + 0x3c4070)
            || (vtable == base + 0x374b760 && callback == base + 0x3c4060)))
            return Status::failure(FailureCode::UnsupportedContent);
        if (!reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(provider + 8))
            return Status::failure(FailureCode::GenerationMismatch);
        const auto charged = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base + 0xd50dc0)(48, 0);
        if (charged < 48 || charged > SIZE_MAX - bytes) return Status::failure(FailureCode::CapacityExceeded);
        bytes += charged;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

bool Sc6ReplayVfxState::SameRequest(std::uintptr_t base, const std::byte* a, const std::byte* b) noexcept
{
    __try
    {
        // Native copy 1403ADD60 omits padding/residue in the value projection.
        if (std::memcmp(a, b, 5) || std::memcmp(a + 8, b + 8, 16)
            || std::memcmp(a + 0x20, b + 0x20, 0x34) || std::memcmp(a + 0x58, b + 0x58, 0x18)
            || At<int>(a, 0xa0) != At<int>(b, 0xa0)) return false;
        if (!At<int>(a, 0xa0)) return true;
        const auto* pa = Provider(a); const auto* pb = Provider(b);
        if (!pa || !pb || pa == pb || std::memcmp(pa, pb, 24)
            || At<std::uint64_t>(pa, 0x20) != At<std::uint64_t>(pb, 0x20)) return false;
        // Bone providers copy selectors at +18. Chest providers leave that
        // range uninitialized. Both clone only the initialized 0x28 prefix.
        return At<std::uintptr_t>(pa, 0) == base + 0x374b760
            || (At<std::uintptr_t>(pa, 0) == base + 0x326c0c8 && std::memcmp(pa + 0x18, pb + 0x18, 8) == 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayVfxState::CaptureUnchecked(std::uintptr_t base, void* battle, std::size_t budget, std::span<void* const> components, std::span<void* const> companion_owners)
{
    std::array<Header, 2> arrays{};
    auto status = ReadManager(battle, manager_, sequence_, arrays);
    if (!status.ok()) return status;
    base_ = base; thread_ = GetCurrentThreadId();
    if (!Bind(base, manager_weak_, manager_)) return Status::failure(FailureCode::GenerationMismatch);
    status = ValidateEmptyNormalizer();
    if (!status.ok()) return status;
    counts_ = {arrays[0].count, arrays[1].count};
    slot_capacities_ = {arrays[0].capacity, arrays[1].capacity};
    const auto count = static_cast<std::size_t>(counts_[0]) + counts_[1];
    if (count > budget / sizeof(Slot)) return Status::failure(FailureCode::CapacityExceeded);
    std::size_t providers = 0;
    for (const auto& array : arrays)
        for (int i = 0; i < array.count; ++i)
        {
            status = ValidateProvider(base, array.data + i * 0xc0 + 0x10, providers);
            if (!status.ok()) return status;
        }
    if (providers > budget - count * sizeof(Slot)) return Status::failure(FailureCode::CapacityExceeded);
    slots_ = std::make_unique<Slot[]>(count); capacity_ = count; provider_bytes_ = providers;
    status = CaptureTables(budget);
    if (status.ok()) status = CaptureDefinitionMap(budget);
    if (!status.ok()) return status;
    for (const auto& array : arrays)
        for (int i = 0; i < array.count; ++i)
        {
            const auto* source = array.data + i * 0xc0;
            auto& slot = slots_[constructed_];
            if (!CopyBytes(slot.bytes.data(), source, 12)) return Status::failure(FailureCode::ContextUnavailable);
            const auto actor = At<std::uintptr_t>(slot.bytes.data(), 0);
            // Native manager loops preserve null slots; retain their request
            // and position without inventing a live component binding.
            if (actor && !Bind(base, slot.actor_weak, actor)) return Status::failure(FailureCode::GenerationMismatch);
            reinterpret_cast<void* (*)(void*, const void*)>(base + 0x3add60)(slot.bytes.data() + 0x10, source + 0x10);
            ++constructed_;
            if (!SameRequest(base, slot.bytes.data() + 0x10, source + 0x10)) return Status::failure(FailureCode::CaptureFailed);
        }
    status = CaptureTilePools(budget, components);
    if (status.ok()) status = RetainOwners(budget, companion_owners);
    if (!status.ok()) return status;
    valid_ = true;
    return ValidateHeld(base, battle);
}

Status Sc6ReplayVfxState::Capture(std::uintptr_t base, void* battle, std::size_t budget, std::span<void* const> components, std::span<void* const> companion_owners) noexcept
{
    if (owned_bytes() >= budget) return Status::failure(FailureCode::CapacityExceeded);
    if (!battle || !base || base != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))
        return Status::failure(FailureCode::IdentityMismatch);
    try
    {
        Sc6ReplayVfxState next;
        const auto status = next.CaptureUnchecked(base, battle, budget - owned_bytes(), components, companion_owners);
        if (status.ok()) Swap(next);
        capture_rejection_ = status.ok() ? CaptureRejection{} : next.capture_rejection_;
        return status;
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

bool Sc6ReplayVfxState::RetainsComponent(const void* component) const noexcept
{
    if(!valid_ || !component || !object_lease_)return false;
    for(const auto& row:components_)
        if(row.address==reinterpret_cast<std::uintptr_t>(component))
            return Live(base_,row.weak,row.address);
    return false;
}

Status Sc6ReplayVfxState::ValidateRetainedOwners() const noexcept
{
    if(thread_!=GetCurrentThreadId())return Status::failure(FailureCode::WrongThread);
    if(!valid_ || !object_lease_)return Status::failure(FailureCode::IllegalTransition);
    return object_lease_->Validate();
}

Status Sc6ReplayVfxState::ValidateReconstructionDependencies() const noexcept
{
    if(thread_!=GetCurrentThreadId())return Status::failure(FailureCode::WrongThread);
    if(!valid_ || !reconstruction_lease_)return Status::failure(FailureCode::IllegalTransition);
    return reconstruction_lease_->Validate();
}

Status Sc6ReplayVfxState::VisitSingleGpuComponents(void* context,Status(*visit)(void*,std::uintptr_t)) const noexcept
{
    if(!visit)return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateRetainedOwners();if(!status.ok())return status;
    if(counts_[0]<0 || static_cast<std::size_t>(counts_[0])>constructed_)
        return Status::failure(FailureCode::GenerationMismatch);
    for(const auto& component:components_) {
        if(!component.lux)continue;
        // World inventory also contains persistent stage components under
        // different outer actors. The bounded constructor contract belongs
        // to primary manager particles; all other owners remain captured and
        // leased, and still require their original identity during restore.
        bool managed=false;
        for(int i=0;i<counts_[0];++i)
            managed|=At<std::uintptr_t>(slots_[i].bytes.data(),0)==component.address;
        if(!managed)continue;
        std::size_t live{},gpu{};
        for(auto emitter:component.emitters)live+=emitter!=0;
        for(const auto& emitter:gpu_owners_)gpu+=emitter.component==component.address;
        if(!live || gpu>component.emitters.size() || component.emitters.size()>128)continue;
        status=visit(context,component.address);if(!status.ok())return status;
    }
    return ValidateRetainedOwners();
}

Status Sc6ReplayVfxState::ValidateReconstructionBindings(std::span<const ReconstructionBinding> bindings) const noexcept
{
    auto status=ValidateReconstructionDependencies();
    if(!status.ok())return status;
    if(bindings.size()!=components_.size())return Status::failure(FailureCode::GenerationMismatch);
    for(std::size_t i=0;i<bindings.size();++i) {
        const auto& binding=bindings[i];const auto& identity=binding.identity;
        if(!binding.lease || !identity.source || !identity.target)
            return Status::failure(FailureCode::GenerationMismatch);
        for(std::size_t j=0;j<i;++j)
            if(bindings[j].identity.source==identity.source || bindings[j].identity.target==identity.target)
                return Status::failure(FailureCode::GenerationMismatch);
        const ComponentBinding* captured{};
        for(const auto& row:components_) {
            if(row.address==identity.source) {
                if(captured || row.weak!=identity.source_weak)return Status::failure(FailureCode::GenerationMismatch);
                captured=&row;
            }
            if(row.address==identity.target && row.address!=identity.source)
                return Status::failure(FailureCode::GenerationMismatch);
        }
        if(!captured)return Status::failure(FailureCode::GenerationMismatch);
        const bool unchanged=identity.source==identity.target;
        if(unchanged!=(identity.source_weak==identity.target_weak)
            || (!unchanged && !captured->lux))return Status::failure(FailureCode::GenerationMismatch);
        status=binding.lease->ValidateObject(reinterpret_cast<void*>(identity.target));
        if(!status.ok())return status;
        if(!Live(base_,identity.target_weak,identity.target))return Status::failure(FailureCode::GenerationMismatch);
        std::uintptr_t vtable{},particle_template{};
        if(!CopyBytes(&vtable,reinterpret_cast<void*>(identity.target),sizeof(vtable))
            || !CopyBytes(&particle_template,reinterpret_cast<void*>(identity.target+0x808),sizeof(particle_template)))
            return Status::failure(FailureCode::ContextUnavailable);
        if(particle_template!=captured->particle_template
            || (captured->lux && vtable!=base_+0x335db28))return Status::failure(FailureCode::GenerationMismatch);
    }
    return Status::success();
}

Status Sc6ReplayVfxState::PrepareReconstructionBindings(const Sc6ReplayVfxState& current,
    std::size_t budget,std::vector<ReconstructionBinding>& output,ReconstructionFactory factory,ReconstructionFailure* failure) const noexcept
{
    if(failure)*failure={};
    if (!output.empty() || !factory.construct) return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidateReconstructionDependencies();
    if (!status.ok()) return status;
    if (!current.valid_ || current.base_!=base_ || current.manager_!=manager_
        || current.manager_weak_!=manager_weak_ || current.thread_!=thread_ || !current.object_lease_)
        return Status::failure(FailureCode::GenerationMismatch);
    status=current.object_lease_->Validate();
    if (!status.ok()) return status;
    const auto previous=output.capacity()*sizeof(ReconstructionBinding);
    if (previous>budget || (components_.size()>output.capacity()
        && components_.size()>(budget-previous)/sizeof(ReconstructionBinding)))
        return Status::failure(FailureCode::CapacityExceeded);
    try {
        output.reserve(components_.size());
        // Validate the whole supported shape before asking the factory to
        // create the first owner. Original source UObjects are never read.
        std::size_t replacements{};
        for (const auto& component : components_) {
            const auto found=std::find_if(current.components_.begin(),current.components_.end(),
                [&](const auto& row){return row.address==component.address;});
            if (found!=current.components_.end()) {
                if (found->weak!=component.weak) return Status::failure(FailureCode::GenerationMismatch);
                continue;
            }
            std::size_t roots{},gpu{},managed{};
            for (auto emitter:component.emitters) roots+=emitter!=0;
            for (const auto& owner:gpu_owners_) if (owner.component==component.address) {
                if (owner.ordinal>=component.emitters.size()) return Status::failure(FailureCode::GenerationMismatch);
                ++gpu;
            }
            for (int i=0;i<counts_[0];++i) managed+=At<std::uintptr_t>(slots_[i].bytes.data(),0)==component.address;
            // Every live root must have exactly one captured typed image.
            // The fresh constructor supplies the GPU root; CPU slots remain
            // null until private native-allocated CPU images are published.
            bool typed=true;
            for(std::size_t ordinal=0;ordinal<component.emitters.size();++ordinal) {
                std::size_t images{};
                for(const auto& owner:gpu_owners_)images+=owner.component==component.address && owner.ordinal==ordinal;
                for(const auto& owner:cpu_emitters_)images+=owner.component==component.address && owner.ordinal==ordinal;
                if(images!=(component.emitters[ordinal]?1u:0u))typed=false;
            }
            for(const auto& owner:cpu_emitters_)if(owner.component==component.address && owner.ordinal>=component.emitters.size())typed=false;
            const bool source_live=Live(base_,component.weak,component.address);
            if (!component.lux || !roots || gpu>roots || !typed || managed!=1 || component.emitters.size()>128 || source_live) {
                if(failure)*failure={component.address,roots,gpu,managed,component.emitters.size(),component.lux,source_live};
                return Status::failure(FailureCode::UnsupportedContent);
            }
            if (++replacements>ParticleBirthSet::capacity) return Status::failure(FailureCode::CapacityExceeded);
        }
        for (const auto& component : components_) {
            ReconstructionBinding binding{};
            const auto found=std::find_if(current.components_.begin(),current.components_.end(),
                [&](const auto& row){return row.address==component.address;});
            if (found!=current.components_.end()) {
                binding.identity={component.address,component.address,component.weak,component.weak};
                binding.lease=current.object_lease_.get();
            } else {
                ReconstructionRequest request{component.address,component.weak,component.emitters.size(),component.emitters.size()};
                for (const auto& owner:gpu_owners_) if (owner.component==component.address) {
                    if(request.gpu_count==request.gpu_ordinals.size())return Status::failure(FailureCode::CapacityExceeded);
                    request.gpu_ordinals[request.gpu_count++]=request.gpu_ordinal=owner.ordinal;
                }
                if (request.gpu_ordinal>request.emitter_count) return Status::failure(FailureCode::GenerationMismatch);
                status=factory.construct(factory.context,request,binding);
                if (!status.ok()) return status;
                if (binding.identity.source!=request.source || binding.identity.source_weak!=request.weak
                    || binding.identity.target==request.source || !binding.lease)
                    return Status::failure(FailureCode::GenerationMismatch);
                if (std::any_of(current.components_.begin(),current.components_.end(),
                    [&](const auto& row){return row.address==binding.identity.target;}))
                    return Status::failure(FailureCode::GenerationMismatch);
            }
            output.push_back(binding);
        }
        status=current.object_lease_->Validate();
        return status.ok()?ValidateReconstructionBindings(output):status;
    } catch (...) {return Status::failure(FailureCode::CapacityExceeded);}
}

Status Sc6ReplayVfxState::ApplyFreshComponentValues(
    const Sc6ReplayCpuEmitterState::ComponentReplacement& binding,bool& dirty) const noexcept
{
    if(dirty || !valid_ || thread_!=GetCurrentThreadId())return Status::failure(FailureCode::IllegalTransition);
    const ComponentBinding* image{};
    for(const auto& c:components_)if(c.address==binding.source) {
        if(image || c.weak!=binding.source_weak)return Status::failure(FailureCode::GenerationMismatch);
        image=&c;
    }
    if(!image || !image->lux || !image->particle_template || !binding.target
        || binding.target==binding.source || binding.target_weak==binding.source_weak)
        return Status::failure(FailureCode::GenerationMismatch);
    __try {
        auto* target=reinterpret_cast<void*>(binding.target);
        if(!Live(base_,binding.target_weak,binding.target)
            || At<std::uintptr_t>(target,0)!=base_+0x335db28
            || At<std::uintptr_t>(target,0x808)!=image->particle_template
            || !(At<unsigned>(target,0x188)&1) || (At<unsigned>(target,0x188)&0x60000)
            || At<std::uintptr_t>(target,0x790) || At<int>(target,0x7a0)
            || At<int>(target,0xa58) || At<std::uintptr_t>(target,0xa70)
            || At<unsigned char>(target,0xa80) || At<unsigned char>(target,0xa81))
            return Status::failure(FailureCode::RestorePreflightFailed);
        std::size_t bytes{};
        for(const auto span:component_spans) {
            if(span.lux && !image->lux)continue;
            if(span.bytes>image->values.size()-bytes)return Status::failure(FailureCode::CapacityExceeded);
            bytes+=span.bytes;
        }
        const auto flags=At<unsigned>(target,0x188);
        dirty=true;
        std::size_t cursor{};
        for(const auto span:component_spans) {
            if(span.lux && !image->lux)continue;
            std::memcpy(static_cast<std::byte*>(target)+span.offset,image->values.data()+cursor,span.bytes);
            cursor+=span.bytes;
        }
        // Native registration/creation/render flags belong to the fresh owner.
        // Restore only captured activation state, without replaying activation.
        const auto restored_flags=(flags&~0x60000u)|image->active_flags;
        std::memcpy(static_cast<std::byte*>(target)+0x188,&restored_flags,sizeof(restored_flags));
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER){return Status::failure(FailureCode::RestoreWriteFailed);}
}
bool Sc6ReplayVfxState::retained_owners_live() const noexcept
{
    return ValidateRetainedOwners().ok();
}

Status Sc6ReplayVfxState::ConstructFreshParticleOwner(std::uintptr_t source,std::uintptr_t target,std::size_t budget,
    Sc6ReplayCpuEmitterState::ComponentReplacement& binding,Sc6ReplayCpuEmitterState::FreshGpuOwner& owner,std::size_t ordinal) const noexcept
{
    if(!valid_ || thread_!=GetCurrentThreadId() || !source || !target || source==target)
        return Status::failure(FailureCode::IllegalTransition);
    const ComponentBinding* component{};
    for(const auto& c:components_)if(c.address==source){if(component)return Status::failure(FailureCode::GenerationMismatch);component=&c;}
    const GpuOwner* gpu{};
    for(const auto& g:gpu_owners_)if(g.component==source && (ordinal==SIZE_MAX || g.ordinal==ordinal)){if(gpu)return Status::failure(FailureCode::UnsupportedContent);gpu=&g;}
    if(ordinal!=SIZE_MAX && !gpu)return Status::failure(FailureCode::GenerationMismatch);
    if(!component || (!gpu && std::none_of(cpu_emitters_.begin(),cpu_emitters_.end(),[&](const auto& e){return e.component==source;})))
        return Status::failure(FailureCode::UnsupportedContent);
    binding={source,target,component->weak,{}};
    if(!Bind(base_,binding.target_weak,target))return Status::failure(FailureCode::GenerationMismatch);
    return gpu?gpu->image.ConstructFreshGpuOwner(binding,budget,owner):Status::success();
}

Status Sc6ReplayVfxState::QueueFreshGpuRetirement(const Sc6ReplayCpuEmitterState::ComponentReplacement& binding,
    Sc6ReplayCpuEmitterState::FreshGpuOwner& owner,std::size_t ordinal) const noexcept
{
    if(!valid_ || thread_!=GetCurrentThreadId())return Status::failure(FailureCode::IllegalTransition);
    const GpuOwner* gpu{};
    for(const auto& g:gpu_owners_)if(g.component==binding.source && (ordinal==SIZE_MAX || g.ordinal==ordinal)){if(gpu)return Status::failure(FailureCode::UnsupportedContent);gpu=&g;}
    return gpu?gpu->image.QueueFreshGpuRetirement(binding,owner):Status::failure(FailureCode::UnsupportedContent);
}
void Sc6ReplayVfxState::DescribeCapturedReferences(Sc6ReplayObjectLease::FailureWitness& output) const noexcept
{
    // Read immutable copied request/delegate bytes only. In particular, do not
    // resolve the dead UObject or interpret a zero count as lifetime admission.
    output.captured_reference_inventory_valid=false;
    output.captured_provider_users=output.captured_completion_users=output.captured_own_completions=0;
    output.captured_manager_listeners=output.captured_listener_users=0;
    if(!output.original || !output.serial || output.index<0)return;
    const std::array<std::int32_t,2> weak{output.index,output.serial};
    const auto matches=[&](const void* p) {return std::memcmp(p,weak.data(),sizeof(weak))==0;};
    __try {
        for(std::size_t i=0;i<constructed_;++i) {
            const auto* request=slots_[i].bytes.data()+0x10;
            const auto count=At<int>(request,0xa0);
            const auto* provider=Provider(request);
            if(!count) {if(provider)return;continue;}
            if(count!=3 || !provider)return;
            output.captured_provider_users+=matches(provider+8);
        }
        for(const auto& component:components_) {
            if(component.completions.count>component.completions.entries.size())return;
            if(component.address==reinterpret_cast<std::uintptr_t>(output.original)) {
                if(component.weak!=weak)return;
                output.captured_own_completions=component.completions.count;
            }
            for(std::size_t i=0;i<component.completions.count;++i)
                output.captured_completion_users+=matches(component.completions.entries[i].data());
        }
        const auto& listeners=tables_[0];
        if(listeners.count && !listeners.bytes)return;
        output.captured_manager_listeners=listeners.count;
        for(std::size_t i=0;i<listeners.count;++i)
            output.captured_listener_users+=matches(listeners.bytes.get()+16*i);
        output.captured_reference_inventory_valid=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return;}
}

void Sc6ReplayVfxState::DiagnoseRetainedOwnerFailure(const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept
{
    DiagnoseOwnerFailure(object_lease_.get(),context,sink);
}

void Sc6ReplayVfxState::DiagnoseReconstructionOwnerFailure(const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept
{
    DiagnoseOwnerFailure(reconstruction_lease_.get(),context,sink);
}

void Sc6ReplayVfxState::DiagnoseOwnerFailure(const Sc6ReplayObjectLease* lease,const void* context,Sc6ReplayObjectLease::FailureSink sink) const noexcept
{
    if(!lease || !sink)return;
    struct Context {const Sc6ReplayVfxState* state;const void* user;Sc6ReplayObjectLease::FailureSink sink;};
    const Context diagnostic{this,context,sink};
    lease->DiagnoseFailure(&diagnostic,[](const void* opaque,const Sc6ReplayObjectLease::FailureWitness& failure) {
        const auto& context=*static_cast<const Context*>(opaque);
        auto witness=failure;
        context.state->DescribeCapturedReferences(witness);
        const auto address=reinterpret_cast<std::uintptr_t>(failure.original);
        // Distinguish a persistent manager request from a world-only effect
        // using the already captured records. No dying UObject is inspected.
        for(std::size_t i=0;address && i<context.state->constructed_;++i) {
            const auto& slot=context.state->slots_[i];
            if(At<std::uintptr_t>(slot.bytes.data(),0)!=address)continue;
            if(!witness.captured_manager_slots) {
                witness.captured_slot_id=At<std::int32_t>(slot.bytes.data(),8);
                witness.captured_slot_kind=At<unsigned char>(slot.bytes.data(),0x14);
            }
            ++witness.captured_manager_slots;
        }
        for(const auto& emitter:context.state->cpu_emitters_)
            witness.captured_cpu_emitters+=emitter.component==address;
        for(const auto& emitter:context.state->gpu_owners_)
            witness.captured_gpu_emitters+=emitter.component==address;
        // Only immutable captured metadata is read here. The original UObject
        // may already be gone; never dereference it to classify the dependency.
        for(const auto& component:context.state->components_) {
            witness.captured_attachment_users+=component.attach_parent==address;
            for(const auto& roots:component.material_roots)
                for(int i=0;i<roots.count;++i) {
                    witness.captured_material_users+=roots.entries[i].object==address;
                    witness.captured_material_parent_users+=roots.entries[i].parent==address;
                }
            if(component.address!=address)continue;
            witness.captured_component=true;witness.captured_component_flags=component.active_flags;
            witness.captured_emitters=component.emitters.size();
            std::size_t cursor{};
            for(const auto& span:component_spans) {
                if(span.lux && !component.lux)continue;
                if(span.offset==0x830)std::memcpy(&witness.captured_template_flags,component.values.data()+cursor,4);
                if(span.offset==0x8b8)std::memcpy(&witness.captured_auto_destroy,component.values.data()+cursor,4);
                cursor+=span.bytes;
            }
        }
        context.sink(context.user,witness);
    });
}

Status Sc6ReplayVfxState::ValidateHeld(std::uintptr_t base, void* battle) const noexcept
{
    if (!valid_ || base != base_ || thread_ != GetCurrentThreadId() || !Live(base, manager_weak_, manager_))
        return Status::failure(FailureCode::GenerationMismatch);
    if (!object_lease_) return Status::failure(FailureCode::GenerationMismatch);
    const auto retained = object_lease_->Validate();
    if (!retained.ok()) return retained;
    std::uintptr_t manager{}; std::int32_t sequence{}; std::array<Header, 2> arrays{};
    auto status = ReadManager(battle, manager, sequence, arrays);
    if (!status.ok()) return status;
    if (manager != manager_ || sequence != sequence_ || arrays[0].count != counts_[0] || arrays[1].count != counts_[1])
        return Status::failure(FailureCode::GenerationMismatch);
    status = ValidateEmptyNormalizer();
    if (!status.ok()) return status;
    if (!SameTables() || !SameDefinitionMap() || !SameTilePools() || !ValidateOwnerPartition().ok()) return Status::failure(FailureCode::GenerationMismatch);
    std::size_t ordinal = 0;
    for (const auto& array : arrays)
        for (int i = 0; i < array.count; ++i, ++ordinal)
        {
            const auto* source = array.data + i * 0xc0; const auto& slot = slots_[ordinal];
            if (!SameIdentity(source, slot.bytes.data())
                || (At<std::uintptr_t>(slot.bytes.data(), 0)
                    && !Live(base, slot.actor_weak, At<std::uintptr_t>(slot.bytes.data(), 0)))
                || !SameRequest(base, slot.bytes.data() + 0x10, source + 0x10))
                return Status::failure(FailureCode::GenerationMismatch);
        }
    return Status::success();
}

Status Sc6ReplayVfxState::PrepareCpuEmitter(std::size_t index, std::size_t budget,
    void*& component, std::size_t& ordinal, Sc6ReplayCpuEmitterState::Prepared& output) const noexcept
{
    component = nullptr; ordinal = 0;
    if (!valid_ || thread_ != GetCurrentThreadId() || !Live(base_, manager_weak_, manager_))
        return Status::failure(FailureCode::GenerationMismatch);
    if (!object_lease_) return Status::failure(FailureCode::GenerationMismatch);
    const auto retained = object_lease_->Validate();
    if (!retained.ok()) return retained;
    if (index >= cpu_emitters_.size()) return Status::failure(FailureCode::RestorePreflightFailed);
    const auto used = owned_bytes();
    if (used > budget) return Status::failure(FailureCode::CapacityExceeded);
    const auto& emitter = cpu_emitters_[index];
    // Remaining allowance excludes the already charged image itself.
    const auto status = emitter.image.PrepareReplacement(budget - used + emitter.image.owned_bytes(), output);
    if (status.ok()) { component = reinterpret_cast<void*>(emitter.component); ordinal = emitter.ordinal; }
    return status;
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateOwners() const noexcept
{
    const auto status = ValidateLifetime();
    if (!status.ok()) return status;
    std::uint64_t epoch{};
    return CopyBytes(&epoch, reinterpret_cast<void*>(target_->base_ + 0x4197170), sizeof(epoch)) && epoch == epoch_
        ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateLifetime() const noexcept
{
    if (!target_ || !current_ || !component_ || !battle_ || !target_->valid_ || !current_->valid_
        || target_->base_ != current_->base_ || target_->manager_ != current_->manager_
        || target_->manager_weak_ != current_->manager_weak_
        || target_->thread_ != GetCurrentThreadId() || current_->thread_ != GetCurrentThreadId()
        || !target_->object_lease_ || !current_->object_lease_)
        return Status::failure(FailureCode::GenerationMismatch);
    const auto a = reconstructed_ ? target_->ValidateReconstructionDependencies() : target_->object_lease_->Validate();
    if (!a.ok()) return a;
    const auto b = current_->object_lease_->Validate();
    if (!b.ok()) return b;
    if (!Live(target_->base_, weak_, component_))
        return Status::failure(FailureCode::GenerationMismatch);
    return Status::success();
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateCurrentB() const noexcept
{
    const auto status = ValidateOwners();
    return status.ok() ? current_->ValidateHeld(current_->base_, battle_) : status;
}

Status Sc6ReplayVfxState::ParticleBirth::ReadRenderBinding(RenderBinding& output) const noexcept
{
    output = {};
    const auto status = ValidateCurrentB();
    if (!status.ok()) return status;
    const GpuOwner* selected = nullptr;
    for (const auto& owner : current_->gpu_owners_)
        if (owner.component == component_)
        {
            // The bounded birth has one GPU emitter. More than one needs a
            // multi-entry registry transaction, not silent partial exclusion.
            if (selected) return Status::failure(FailureCode::UnsupportedContent);
            selected = &owner;
        }
    if (!selected) return Status::failure(FailureCode::UnsupportedContent);
    output = {current_->base_, component_, selected->emitter, selected->render_storage,
        selected->system, selected->pool, epoch_};
    return Status::success();
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateRetirement(const char** failed_check) const noexcept
{
    if(failed_check)*failed_check="owners";
    auto status = ValidateOwners();
    return status.ok()?ValidateRetirementBody(failed_check):status;
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateQuarantinedRetirement() const noexcept
{
    const auto status=ValidateQuarantined();
    return status.ok()?ValidateRetirementBody():status;
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateRetirementBody(const char** failed_check) const noexcept
{
    auto status=Status::success();
    __try
    {
        const auto base = current_->base_;
        const auto* component = reinterpret_cast<const std::byte*>(component_);
        const auto table = At<std::uintptr_t>(component, 0);
        const auto flags = At<std::uint32_t>(component, 0x188);
        if(failed_check)*failed_check="native_identity";
        if (table != base + 0x335db28
            || At<std::uintptr_t>(reinterpret_cast<void*>(table), 0x360) != base + 0x8cecf0
            || At<std::uintptr_t>(reinterpret_cast<void*>(table), 0x370) != base + 0x1da5910
            || At<std::uintptr_t>(reinterpret_cast<void*>(table), 0x1f8) != base + 0xf71540)
            return Status::failure(FailureCode::IdentityMismatch);
        // Physics-state teardown, navigation/overlap callbacks, attachment
        // descendants and shared material roots need additional participants.
        // None may be silently omitted from the fixed birth experiment.
        if(failed_check)*failed_check="component_flags";
        if (!(flags & 1) || (flags & (0x10000000u | 0x200000u | 4u)))
            return Status::failure(FailureCode::UnsupportedContent);
        if(failed_check)*failed_check="navigation";
        if(At<std::uint8_t>(component,0x3f8) & 2) return Status::failure(FailureCode::UnsupportedContent);
        if(failed_check)*failed_check="overlaps";
        if(At<std::int32_t>(component,0x6a8)) return Status::failure(FailureCode::UnsupportedContent);
        if(failed_check)*failed_check="attachment_children";
        if(At<std::int32_t>(component,0x1e0)) return Status::failure(FailureCode::UnsupportedContent);
        if(failed_check)*failed_check="attachment_parent";
        if(At<std::uintptr_t>(component,0x1d0)) return Status::failure(FailureCode::UnsupportedContent);
        // Both material-root arrays are covered by captured membership,
        // exact component ownership and the enclosing image's GC leases below.
        // Native1408D4460 clears root flags only at admitted retirement; no
        // root flags or historical material parameters are installed on undo.
        if(failed_check)*failed_check="particle_task";
        if(At<std::uintptr_t>(component,0xa70)) return Status::failure(FailureCode::UnsupportedContent);
        if(failed_check)*failed_check="particle_completion";
        if(At<std::uint8_t>(component,0xa81)) return Status::failure(FailureCode::UnsupportedContent);

        // 141D42DE0 -> 1421F9C60 dispatches the actual EndPlay UFunction.
        // 140F71540 returns before invocation when non-native script Num=0.
        if(failed_check)*failed_check="end_play_script";
        if (flags & 0x08000000u)
        {
            const auto name = At<std::uint64_t>(reinterpret_cast<void*>(base), 0x43b5538);
            auto* function = reinterpret_cast<void* (*)(const void*, std::uint64_t)>(base + 0xf6e0e0)(component, name);
            if (!function || (At<std::uint32_t>(function, 0x88) & 0x400)
                || At<std::uint32_t>(function, 0x50))
                return Status::failure(FailureCode::UnsupportedContent);
        }
        // FinalizeParticleEmitterInstances broadcasts this global collection
        // only for active components. Empty means no callback-owned mutation.
        if(failed_check)*failed_check="completion_callbacks";
        if ((flags & 0x40000) && (At<std::int32_t>(reinterpret_cast<void*>(base + 0x40956c0), 0x50)
            || At<std::int32_t>(reinterpret_cast<void*>(base + 0x40956c0), 0x64)))
            return Status::failure(FailureCode::UnsupportedContent);
        const ComponentBinding* captured = nullptr;
        for (const auto& binding : current_->components_) if (binding.address == component_) captured = &binding;
        if(failed_check)*failed_check="captured_component";
        if (!captured) return Status::failure(FailureCode::GenerationMismatch);
        if(failed_check)*failed_check="component_boundary";
        status = CheckComponentBoundary(base, *captured);
        if (!status.ok()) return status;
        if(failed_check)*failed_check="cpu_emitter_graph";
        for (const auto& cpu : current_->cpu_emitters_)
            if (cpu.component == component_)
            {
                if (cpu.ordinal >= captured->emitters.size()) return Status::failure(FailureCode::GenerationMismatch);
                status = cpu.image.ValidateNativeGraph(reinterpret_cast<void*>(captured->emitters[cpu.ordinal]));
                if (!status.ok()) return status;
            }
        if(failed_check)*failed_check="gpu_emitter_graph";
        for (const auto& gpu : current_->gpu_owners_)
            if (gpu.component == component_)
            {
                status = gpu.image.ValidateNativeGraph(reinterpret_cast<void*>(gpu.emitter));
                if (!status.ok()) return status;
            }
        if(failed_check)*failed_check=nullptr;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::ParticleBirth::RetireAfterCommit() const noexcept
{
    const auto status = ValidateRetirement();
    if (!status.ok()) return status;
    return RetireValidated();
}

Status Sc6ReplayVfxState::ParticleBirth::RetireValidated() const noexcept
{
    __try
    {
        // Registry exclusion has already committed. A now owns the shared
        // free prefix; suppress only the displaced B tile return, then use
        // the native destructor for its emitter/proxy/attachment ownership.
        for (const auto& gpu : current_->gpu_owners_)
            if (gpu.component == component_)
            {
                if (At<std::int32_t>(reinterpret_cast<void*>(gpu.render_storage), 0x240) != -1)
                    return Status::failure(FailureCode::RestoreVerificationFailed);
                *reinterpret_cast<std::int32_t*>(gpu.emitter + 0x1f0) = 0;
            }
        reinterpret_cast<void (*)(void*, bool)>(current_->base_ + 0x8cecf0)(reinterpret_cast<void*>(component_), false);
        const auto* component = reinterpret_cast<void*>(component_);
        if ((At<std::uint32_t>(component, 0x188) & 7) || At<std::int32_t>(component, 0xa58)
            || At<std::uintptr_t>(component, 0xa70) || Live(current_->base_, weak_, component_))
            return Status::failure(FailureCode::RestoreVerificationFailed);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreWriteFailed); }
}

Status Sc6ReplayVfxState::PrepareParticleBirth(const Sc6ReplayVfxState& current,
    void* battle, ParticleBirth& output) const noexcept
{
    if (output.target_ || output.current_) return Status::failure(FailureCode::IllegalTransition);
    // The bounded combat transaction admits exactly one new primary VFX slot,
    // with all previous component identities retained. No missing component,
    // secondary-table change or unrelated scheduler registration is inferred.
    if (!valid_ || !current.valid_ || counts_[0] < 0 || counts_[0] == INT32_MAX
        || current.counts_[0] != counts_[0] + 1 || current.counts_[1] != counts_[1]
        || current.components_.size() != components_.size() + 1)
        return Status::failure(FailureCode::UnsupportedContent);
    ParticleBirth result;
    result.target_ = this;
    result.current_ = &current;
    result.battle_ = battle;
    if (!CopyBytes(&result.epoch_, reinterpret_cast<void*>(base_ + 0x4197170), sizeof(result.epoch_)))
        return Status::failure(FailureCode::ContextUnavailable);
    const auto& born_slot = current.slots_[counts_[0]];
    result.component_ = At<std::uintptr_t>(born_slot.bytes.data(), 0);
    result.weak_ = born_slot.actor_weak;
    auto status = result.ValidateCurrentB();
    if (!status.ok()) return status;
    for (std::size_t i = 0; i < constructed_; ++i)
    {
        const auto& a = slots_[i];
        const auto& b = current.slots_[i + (i >= static_cast<std::size_t>(counts_[0]) ? 1 : 0)];
        if (!SameIdentity(a.bytes.data(), b.bytes.data()) || a.actor_weak != b.actor_weak)
            return Status::failure(FailureCode::GenerationMismatch);
    }
    bool found_birth = false;
    for (const auto& component : current.components_)
    {
        const ComponentBinding* prior = nullptr;
        for (const auto& candidate : components_)
            if (candidate.address == component.address) { prior = &candidate; break; }
        if (prior)
        {
            if (prior->weak != component.weak || !Live(base_, prior->weak, prior->address))
                return Status::failure(FailureCode::GenerationMismatch);
        }
        else
        {
            if (found_birth || component.address != result.component_ || component.weak != result.weak_)
                return Status::failure(FailureCode::GenerationMismatch);
            found_birth = true;
        }
    }
    if (!found_birth) return Status::failure(FailureCode::GenerationMismatch);
    output = result;
    return Status::success();
}

Sc6ReplayVfxState::ParticleBirthSet Sc6ReplayVfxState::ParticleBirthSet::Single(const ParticleBirth& birth) noexcept
{
    ParticleBirthSet result;
    if (birth.component()) { result.members_[0] = birth; result.count_ = 1; }
    return result;
}

Status Sc6ReplayVfxState::ParticleBirthSet::ValidateOwners() const noexcept
{
    for (const auto& member : members()) { const auto status = member.ValidateOwners(); if (!status.ok()) return status; }
    return Status::success();
}
Status Sc6ReplayVfxState::ParticleBirthSet::ValidateCurrentB() const noexcept
{
    const auto status = ValidateOwners();
    return !status.ok() || empty() ? status : members_[0].ValidateCurrentB();
}
Status Sc6ReplayVfxState::ParticleBirthSet::ValidateRetirement() const noexcept
{
    for (const auto& member : members()) { const auto status = member.ValidateRetirement(); if (!status.ok()) return status; }
    return Status::success();
}
Status Sc6ReplayVfxState::ParticleBirthSet::RetireAfterCommit() const noexcept
{
    // Validate the entire batch before its first irreversible destruction.
    // All callbacks/descendants/physics exclusions from the single-owner path
    // remain required. Destroying member 0 intentionally invalidates B's lease;
    // it cannot be used as a fresh all-live precondition for member 1.
    const auto status = ValidateRetirement();
    if (!status.ok()) return status;
    for (const auto& member : members()) {
        const auto retired = member.RetireValidated();
        if (!retired.ok()) return retired; // enclosing owner must not resume
    }
    return Status::success();
}
Status Sc6ReplayVfxState::ParticleBirthSet::ValidateQuarantinedRetirement() const noexcept
{
    for(const auto& member:members()) {
        const auto status=member.ValidateQuarantinedRetirement();if(!status.ok()) return status;
    }
    return Status::success();
}
Status Sc6ReplayVfxState::ParticleBirthSet::RetireQuarantinedAfterCommit() const noexcept
{
    const auto status=ValidateQuarantinedRetirement();if(!status.ok()) return status;
    for(const auto& member:members()) {
        const auto retired=member.RetireValidated();if(!retired.ok()) return retired;
    }
    return Status::success();
}
Status Sc6ReplayVfxState::ParticleBirthSet::ReadRenderBindings(
    std::span<ParticleBirth::RenderBinding> output, std::size_t& count) const noexcept
{
    count = 0;
    const auto status = ValidateCurrentB();
    if (!status.ok()) return status;
    std::size_t required{};
    for (const auto& member : members())
        for (const auto& owner : member.current_->gpu_owners_) if (owner.component == member.component_) ++required;
    if (required > output.size()) return Status::failure(FailureCode::CapacityExceeded);
    for (const auto& member : members())
        for (const auto& owner : member.current_->gpu_owners_) if (owner.component == member.component_)
            output[count++] = {member.current_->base_, member.component_, owner.emitter,
                owner.render_storage, owner.system, owner.pool, member.epoch_};
    return Status::success();
}

Status Sc6ReplayVfxState::PrepareTopology(const Sc6ReplayVfxState& current,
    void* battle, Topology& topology, ParticleBirthSet& births, RetainedSecondaryOwners private_owners,
    std::span<const ReconstructionBinding> bindings) const noexcept
{
    if (!births.empty()) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || !current.valid_ || base_ != current.base_ || manager_ != current.manager_
        || manager_weak_ != current.manager_weak_ || thread_ != current.thread_
        || thread_ != GetCurrentThreadId() || !object_lease_ || !current.object_lease_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = bindings.empty() ? object_lease_->Validate() : ValidateReconstructionBindings(bindings);
    if (status.ok()) status = current.ValidateHeld(base_, battle);
    if (!status.ok()) return status;
    // The ground participant supplies already retained/frozen private B roots.
    // Inventory that separate domain for either primary-particle path. A root
    // must occur exactly once in B's secondary suffix and must not belong to A
    // or to either image's particle-component inventory.
    struct PrivateOwners {std::array<std::uintptr_t,16> owners{};std::size_t count{};} retained;
    if(private_owners.visit) {
        status=private_owners.visit(private_owners.context,&retained,[](void* context,std::uintptr_t owner) {
            auto& rows=*static_cast<PrivateOwners*>(context);
            if(!owner || rows.count==rows.owners.size()
                || std::find(rows.owners.begin(),rows.owners.begin()+rows.count,owner)!=rows.owners.begin()+rows.count)return false;
            rows.owners[rows.count++]=owner;return true;
        });
        if(!status.ok())return status;
    } else if(private_owners.context)return Status::failure(FailureCode::IllegalTransition);
    for(std::size_t p=0;p<retained.count;++p) {
        const auto owner=retained.owners[p];unsigned matches{};
        for(unsigned table=0;table<2;++table) {
            const auto first=table?current.counts_[0]:0;
            for(int i=0;i<current.counts_[table];++i)if(At<std::uintptr_t>(current.slots_[first+i].bytes.data(),0)==owner) {
                if(table!=1 || i<counts_[table])return Status::failure(FailureCode::GenerationMismatch);
                ++matches;
            }
            const auto original=table?counts_[0]:0;
            for(int i=0;i<counts_[table];++i)
                if(At<std::uintptr_t>(slots_[original+i].bytes.data(),0)==owner)return Status::failure(FailureCode::GenerationMismatch);
        }
        if(matches!=1 || std::any_of(current.components_.begin(),current.components_.end(),
            [&](const auto& row){return row.address==owner;})
            || std::any_of(components_.begin(),components_.end(),[&](const auto& row){return row.address==owner;}))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    if (!bindings.empty()) {
        // Native completion removes records and compacts the table. Match the
        // captured actor/slot-id pair, never its obsolete array index. This
        // bounded path reconstructs primary particles only. Secondary private
        // roots above remain owned by their separate B retirement participant.
        const auto replacement = [&](std::uintptr_t source) -> const ReconstructionBinding* {
            for (const auto& binding : bindings) if (binding.identity.source == source) return &binding;
            return nullptr;
        };
        for (const auto& binding : bindings) {
            const auto& id = binding.identity;
            if (id.source == id.target) {
                const auto found = std::find_if(current.components_.begin(), current.components_.end(),
                    [&](const auto& row) { return row.address == id.source && row.weak == id.source_weak; });
                if (found == current.components_.end()) return Status::failure(FailureCode::GenerationMismatch);
            } else {
                if (std::any_of(current.components_.begin(), current.components_.end(),
                    [&](const auto& row) { return row.address == id.source || row.address == id.target; }))
                    return Status::failure(FailureCode::GenerationMismatch);
                unsigned primary{};
                for (int i=0;i<counts_[0];++i)
                    if (At<std::uintptr_t>(slots_[i].bytes.data(),0)==id.source) ++primary;
                if (primary!=1) return Status::failure(FailureCode::UnsupportedContent);
                for (int i=0;i<current.counts_[0]+current.counts_[1];++i) {
                    const auto& slot=current.slots_[i];
                    if (At<std::uintptr_t>(slot.bytes.data(),0)==id.source
                        || At<std::uintptr_t>(slot.bytes.data(),0)==id.target)
                        return Status::failure(FailureCode::GenerationMismatch);
                }
            }
        }
        for (unsigned table=0;table<2;++table) {
            const auto a_start=table?counts_[0]:0;
            const auto b_start=table?current.counts_[0]:0;
            if (counts_[table]<0 || current.counts_[table]<0)
                return Status::failure(FailureCode::GenerationMismatch);
            for (int i=0;i<counts_[table];++i) {
                const auto& a=slots_[a_start+i];
                const auto owner=At<std::uintptr_t>(a.bytes.data(),0);
                const auto* binding=replacement(owner);
                const bool replaced=binding && binding->identity.source!=binding->identity.target;
                if (replaced && (table || a.actor_weak!=binding->identity.source_weak))
                    return Status::failure(FailureCode::UnsupportedContent);
                unsigned matches{},versions{};
                for (int j=0;j<current.counts_[table];++j) {
                    const auto& b=current.slots_[b_start+j];
                    if (At<int>(a.bytes.data(),8)!=At<int>(b.bytes.data(),8)) continue;
                    if(++versions>1)return Status::failure(FailureCode::GenerationMismatch);
                    if(replaced) {
                        // Earlier rolling execution can give the same logical
                        // slot a different native owner. It remains exact B;
                        // the B-only inventory below owns its quarantine and
                        // retirement. Never bind fresh A to that B address.
                        const auto b_owner=At<std::uintptr_t>(b.bytes.data(),0);
                        if(!b_owner || replacement(b_owner))return Status::failure(FailureCode::GenerationMismatch);
                        continue;
                    }
                    if (!SameIdentity(a.bytes.data(),b.bytes.data()) || a.actor_weak!=b.actor_weak)
                        return Status::failure(FailureCode::GenerationMismatch);
                    ++matches;
                }
                if (matches!=(replaced?0u:1u)) return Status::failure(FailureCode::GenerationMismatch);
            }
        }
        ParticleBirthSet prepared;
        std::uint64_t epoch{};
        if (!CopyBytes(&epoch,reinterpret_cast<void*>(base_+0x4197170),sizeof(epoch)))
            return Status::failure(FailureCode::ContextUnavailable);
        for (const auto& component : current.components_) {
            if (replacement(component.address)) continue;
            if (prepared.count_==prepared.capacity) return Status::failure(FailureCode::CapacityExceeded);
            unsigned matches{};
            for (unsigned table=0;table<2;++table) {
                const auto first=table?current.counts_[0]:0;
                for (int i=0;i<current.counts_[table];++i) {
                    const auto& slot=current.slots_[first+i];
                    if (At<std::uintptr_t>(slot.bytes.data(),0)!=component.address) continue;
                    if (slot.actor_weak!=component.weak) return Status::failure(FailureCode::GenerationMismatch);
                    ++matches;
                }
            }
            if (matches!=1) return Status::failure(FailureCode::UnsupportedContent);
            auto& member=prepared.members_[prepared.count_++];
            member.target_=this;member.current_=&current;member.battle_=battle;
            member.component_=component.address;member.weak_=component.weak;member.epoch_=epoch;member.reconstructed_=true;
        }
        // A new non-null slot must belong to a captured B component. Unknown
        // secondary actors cannot be inferred to be safe particle births.
        for (unsigned table=0;table<2;++table) {
            const auto first=table?current.counts_[0]:0;
            const auto a_first=table?counts_[0]:0;
            for (int i=0;i<current.counts_[table];++i) {
                const auto& b=current.slots_[first+i];
                bool found{};
                for (int j=0;j<counts_[table];++j)
                    if (SameIdentity(slots_[a_first+j].bytes.data(),b.bytes.data())) found=true;
                const auto owner=At<std::uintptr_t>(b.bytes.data(),0);
                if (!found && owner && std::none_of(prepared.members().begin(),prepared.members().end(),
                    [&](const auto& member){return member.component()==owner;})
                    && !(table==1 && std::find(retained.owners.begin(),retained.owners.begin()+retained.count,owner)
                        !=retained.owners.begin()+retained.count))
                    return Status::failure(FailureCode::UnsupportedContent);
            }
        }
        status=prepared.ValidateCurrentB();
        if (status.ok()) status=ValidateReconstructionBindings(bindings);
        if (!status.ok()) return status;
        topology=prepared.empty()?Topology::Unchanged:prepared.count_==1?Topology::SingleBirth:Topology::Births;
        births=prepared;
        return Status::success();
    }
    for (unsigned table = 0; table < 2; ++table) {
        if (counts_[table] < 0 || current.counts_[table] < counts_[table])
            return Status::failure(FailureCode::GenerationMismatch);
        const auto a_start = table ? counts_[0] : 0;
        const auto b_start = table ? current.counts_[0] : 0;
        for (int i = 0; i < counts_[table]; ++i)
            if (!SameIdentity(slots_[a_start+i].bytes.data(), current.slots_[b_start+i].bytes.data())
                || slots_[a_start+i].actor_weak != current.slots_[b_start+i].actor_weak)
                return Status::failure(FailureCode::GenerationMismatch);
    }
    // No A-only death/replacement is inferred. An older valid anchor may
    // precede those births, but every owner in that anchor must still survive.
    for (const auto& component : components_) {
        const auto found = std::find_if(current.components_.begin(), current.components_.end(),
            [&](const auto& row) { return row.address == component.address; });
        if (found == current.components_.end() || found->weak != component.weak
            || !Live(base_, component.weak, component.address))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    ParticleBirthSet prepared;
    std::uint64_t epoch{};
    if (!CopyBytes(&epoch, reinterpret_cast<void*>(base_ + 0x4197170), sizeof(epoch)))
        return Status::failure(FailureCode::ContextUnavailable);
    for (const auto& component : current.components_) {
        if (std::any_of(components_.begin(), components_.end(), [&](const auto& a) { return a.address == component.address; })) continue;
        if (prepared.count_ == prepared.capacity) return Status::failure(FailureCode::CapacityExceeded);
        // Every excluded scene component must have exactly one newly authored
        // manager slot. Unrelated stage/actor registration is not a VFX birth.
        unsigned matches{};
        for (unsigned table = 0; table < 2; ++table) {
            const auto first = table ? current.counts_[0] : 0;
            for (int i = 0; i < current.counts_[table]; ++i) {
                const auto& slot = current.slots_[first+i];
                if (At<std::uintptr_t>(slot.bytes.data(), 0) != component.address) continue;
                if (i < counts_[table] || slot.actor_weak != component.weak)
                    return Status::failure(FailureCode::GenerationMismatch);
                ++matches;
            }
        }
        if (matches != 1) return Status::failure(FailureCode::UnsupportedContent);
        auto& member = prepared.members_[prepared.count_++];
        member.target_ = this; member.current_ = &current; member.battle_ = battle;
        member.component_ = component.address; member.weak_ = component.weak; member.epoch_ = epoch;
    }
    // New slots may have completed before B. Their null owner is real native
    // history, retained in B and removed with A's complete manager image.
    // A live suffix owner must be accounted by the component delta above.
    for (unsigned table = 0; table < 2; ++table) {
        const auto first = table ? current.counts_[0] : 0;
        for (int i = counts_[table]; i < current.counts_[table]; ++i) {
            const auto owner = At<std::uintptr_t>(current.slots_[first+i].bytes.data(), 0);
            if (owner && std::none_of(prepared.members().begin(), prepared.members().end(),
                [&](const auto& member) { return member.component() == owner; })
                && !(table==1 && std::find(retained.owners.begin(),retained.owners.begin()+retained.count,owner)
                    !=retained.owners.begin()+retained.count))
                return Status::failure(FailureCode::GenerationMismatch);
        }
    }
    status = prepared.ValidateCurrentB();
    if (!status.ok()) return status;
    topology = prepared.empty() ? Topology::Unchanged : prepared.count_ == 1 ? Topology::SingleBirth : Topology::Births;
    births = prepared;
    return Status::success();
}

Sc6ReplayVfxState::PreparedManager::~PreparedManager() { Clear(); }

Status Sc6ReplayVfxState::PrepareRecoveryRetirement(const Sc6ReplayVfxState& current,
    void* battle, ParticleBirthSet& births) const noexcept
{
    if(!births.empty())return Status::failure(FailureCode::IllegalTransition);
    ParticleBirthSet prepared;
    auto status=PrepareExecutionParticles(current,battle,prepared);
    if(!status.ok())return status;
    // Full recovery additionally needs every secondary actor protected by B.
    // A primary inventory alone cannot authorize whole-manager retirement.
    for(const auto& slot:std::span<const Slot>{current.slots_.get(),current.constructed_}) {
        const auto owner=At<std::uintptr_t>(slot.bytes.data(),0);
        if(!owner)continue;
        if(std::any_of(current.components_.begin(),current.components_.end(),[&](const auto& c){return c.address==owner && c.weak==slot.actor_weak;}))continue;
        if(std::none_of(slots_.get(),slots_.get()+constructed_,[&](const auto& b){return SameIdentity(slot.bytes.data(),b.bytes.data()) && slot.actor_weak==b.actor_weak;}))
            return Status::failure(FailureCode::UnsupportedContent);
    }
    births=prepared;return Status::success();
}

Status Sc6ReplayVfxState::PrepareExecutionParticles(const Sc6ReplayVfxState& current,
    void* battle, ParticleBirthSet& births) const noexcept
{
    if(!births.empty())return Status::failure(FailureCode::IllegalTransition);
    if(!valid_ || !current.valid_ || base_!=current.base_ || manager_!=current.manager_
        || manager_weak_!=current.manager_weak_ || thread_!=current.thread_
        || thread_!=GetCurrentThreadId() || !object_lease_ || !current.object_lease_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto status=object_lease_->Validate();
    if(status.ok())status=current.ValidateHeld(base_,battle);
    if(!status.ok())return status;
    if(current.counts_[0]<0 || current.counts_[1]<0
        || std::size_t(current.counts_[0])+std::size_t(current.counts_[1])!=current.constructed_)
        return Status::failure(FailureCode::GenerationMismatch);
    ParticleBirthSet prepared;std::uint64_t epoch{};
    if(!CopyBytes(&epoch,reinterpret_cast<void*>(base_+0x4197170),sizeof(epoch)))
        return Status::failure(FailureCode::ContextUnavailable);
    for(const auto& component:current.components_) {
        const auto protected_owner=std::find_if(components_.begin(),components_.end(),[&](const auto& b){return b.address==component.address;});
        if(protected_owner!=components_.end()) {
            if(protected_owner->weak!=component.weak)return Status::failure(FailureCode::GenerationMismatch);
            continue;
        }
        if(!component.address || prepared.count_==prepared.capacity)return Status::failure(FailureCode::CapacityExceeded);
        for(const auto& prior:prepared.members())if(prior.component()==component.address)return Status::failure(FailureCode::GenerationMismatch);
        unsigned matches{};
        for(std::size_t i=0;i<current.constructed_;++i) {
            const auto& slot=current.slots_[i];
            if(At<std::uintptr_t>(slot.bytes.data(),0)!=component.address)continue;
            if(i>=std::size_t(current.counts_[0]) || slot.actor_weak!=component.weak)
                return Status::failure(FailureCode::UnsupportedContent);
            ++matches;
        }
        if(matches!=1)return Status::failure(FailureCode::UnsupportedContent);
        auto& member=prepared.members_[prepared.count_++];
        member.target_=this;member.current_=&current;member.battle_=battle;
        member.component_=component.address;member.weak_=component.weak;member.epoch_=epoch;
    }
    // Every primary slot still needs an exact typed particle identity. The
    // secondary suffix is outside this inventory, not admitted for retirement.
    for(const auto& slot:std::span<const Slot>{current.slots_.get(),std::size_t(current.counts_[0])}) {
        const auto owner=At<std::uintptr_t>(slot.bytes.data(),0);
        if(!owner)continue;
        if(std::none_of(current.components_.begin(),current.components_.end(),[&](const auto& c){return c.address==owner && c.weak==slot.actor_weak;}))
            return Status::failure(FailureCode::UnsupportedContent);
    }
    status=prepared.ValidateCurrentB();
    if(!status.ok())return status;
    births=prepared;return Status::success();
}

Status Sc6ReplayVfxState::PrepareCoordinateReconstruction(const Sc6ReplayVfxState& current,
    std::vector<CoordinateRebuild>& output, std::size_t budget, CoordinateFailure* failure,
    std::span<const ReconstructionBinding> bindings) const noexcept
{
    if (failure) *failure = {};
    if (!valid_ || !current.valid_ || !object_lease_ || !current.object_lease_
        || !(bindings.empty()?object_lease_->Validate():ValidateReconstructionBindings(bindings)).ok()
        || !current.object_lease_->Validate().ok())
        return Status::failure(FailureCode::GenerationMismatch);
    if (!output.empty() || gpu_owners_.size() > budget / sizeof(CoordinateRebuild)
        || output.capacity() > budget / sizeof(CoordinateRebuild))
        return Status::failure(FailureCode::CapacityExceeded);
    try {
    const auto old_metadata = output.capacity() * sizeof(CoordinateRebuild);
    if (gpu_owners_.size() > output.capacity()
        && gpu_owners_.size() * sizeof(CoordinateRebuild) > budget - old_metadata)
        return Status::failure(FailureCode::CapacityExceeded);
    output.reserve(gpu_owners_.size());
    for (const auto& a : gpu_owners_)
    {
        const ReconstructionBinding* mapped{};
        for(const auto& binding:bindings)if(binding.identity.source==a.component)mapped=&binding;
        if(mapped && mapped->identity.source!=mapped->identity.target) {
            const auto component=mapped->identity.target;
            if(std::any_of(current.components_.begin(),current.components_.end(),
                [&](const auto& row){return row.address==component || row.address==a.component;}))
                return Status::failure(FailureCode::GenerationMismatch);
            const auto source=std::find_if(components_.begin(),components_.end(),[&](const auto& row){return row.address==a.component;});
            std::array<std::byte,16> array{};std::array<std::byte,0x2a0> root{};std::array<std::byte,0x260> render{};
            std::uintptr_t emitter{},resource{};
            if(source==components_.end() || !CopyBytes(array.data(),reinterpret_cast<void*>(component+0xa50),array.size()))
                return Status::failure(FailureCode::GenerationMismatch);
            const auto count=At<int>(array.data(),8),capacity=At<int>(array.data(),12);
            const auto slots=At<std::uintptr_t>(array.data(),0);
            if(count<0 || std::size_t(count)!=source->emitters.size() || capacity<count || capacity>128 || !slots
                || a.ordinal>=std::size_t(count)
                || !CopyBytes(&emitter,reinterpret_cast<void*>(slots+a.ordinal*8),8) || !emitter
                || !CopyBytes(root.data(),reinterpret_cast<void*>(emitter),root.size()))
                return Status::failure(FailureCode::GenerationMismatch);
            const auto render_owner=At<std::uintptr_t>(root.data(),0x1e0),descriptor=At<std::uintptr_t>(root.data(),0x1d8);
            if(At<std::uintptr_t>(root.data(),0)!=base_+0x394c100 || At<std::uintptr_t>(root.data(),0x18)
                || At<std::uintptr_t>(root.data(),0x1d0)!=a.system || !descriptor || !render_owner
                || !CopyBytes(&resource,reinterpret_cast<void*>(descriptor+0x2b0),8) || resource!=a.coordinate_resource
                || !CopyBytes(render.data(),reinterpret_cast<void*>(render_owner),render.size())
                || At<std::uintptr_t>(render.data(),0)!=base_+0x394bfc0
                || At<std::uintptr_t>(render.data(),0x1f0)!=base_+0x394c000
                || At<int>(render.data(),0x240)!=-1 || At<std::uint8_t>(render.data(),0x28)
                || At<std::uint8_t>(render.data(),0x218) || At<std::uint8_t>(render.data(),0x252)
                || At<std::uintptr_t>(render.data(),0x48))return Status::failure(FailureCode::GenerationMismatch);
            output.push_back({component,emitter,render_owner,a.system,a.pool,descriptor,resource,a.image.vector_field_asset(),
                a.tiles,0,{},true,true,mapped->identity.target_weak});
            output.back().source_render=a.render_storage;
            continue;
        }
        const GpuOwner* b = nullptr;
        for (const auto& owner : current.gpu_owners_)
            if (owner.component == a.component && owner.ordinal == a.ordinal) { b = &owner; break; }
        // The descriptor's compiled resource is an immutable binding here.
        // Changed ordered tile inputs require native buffer reconstruction.
        if (!b || a.system != b->system || a.pool != b->pool || a.coordinate_resource != b->coordinate_resource) {
            std::size_t first{};
            if (b) while (first < a.tiles.size() && first < b->tiles.size() && a.tiles[first] == b->tiles[first]) ++first;
            if (failure) *failure = {a.component, a.render_storage, b ? b->render_storage : 0,
                a.ordinal, a.tiles.size(), b ? b->tiles.size() : 0, first,
                first < a.tiles.size() ? a.tiles[first] : ~0u, b && first < b->tiles.size() ? b->tiles[first] : ~0u,
                b != nullptr, b && a.system == b->system, b && a.pool == b->pool};
            return Status::failure(FailureCode::UnsupportedContent);
        }
        if(a.image.vector_field_asset()!=b->image.vector_field_asset())
            return Status::failure(FailureCode::GenerationMismatch);
        // Every captured render-parameter row needs its typed owner, even
        // when its tile buffers and absent vector field are unchanged.
        // rebuild_tiles remains false for those ownership-only rows.
        {
            std::uintptr_t descriptor{}, resource{};
            if (!CopyBytes(&descriptor, reinterpret_cast<void*>(b->emitter + 0x1d8), 8) || !descriptor
                || !CopyBytes(&resource, reinterpret_cast<void*>(descriptor + 0x2b0), 8) || resource != b->coordinate_resource)
                return Status::failure(FailureCode::GenerationMismatch);
            output.push_back({b->component, b->emitter, b->render_storage, b->system, b->pool, descriptor,
                resource, a.image.vector_field_asset(), a.tiles, static_cast<std::uint32_t>(b->tiles.size()), b->tiles, a.tiles!=b->tiles});
        }
    }
    return Status::success();
    } catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

void Sc6ReplayVfxState::PreparedManager::Clear(bool retire_previous) noexcept
{
    if (published_) return;
    if (target_)
    {
        const auto base = target_->base_;
        const auto& arrays = retire_previous ? previous_ : arrays_;
        for (std::size_t a = 0; a < 2; ++a)
            for (int i = 0; i < arrays[a].count; ++i)
            {
                auto* provider = static_cast<std::byte*>(arrays[a].data) + i * 0xc0 + 0x80;
                reinterpret_cast<void (*)(void*)>(base + 0x3a22d0)(provider);
                if (auto* heap = At<void*>(provider, 0x20))
                    reinterpret_cast<void (*)(void*)>(base + 0xd46a00)(heap);
            }
        for (auto& allocation : allocations_)
            if (auto* data = retire_previous ? allocation.previous : allocation.target)
                reinterpret_cast<void (*)(void*)>(base + 0xd46a00)(data);
    }
    std::vector<Allocation>().swap(allocations_);
    for (auto& bindings : provider_bindings_) std::vector<const std::byte*>().swap(bindings);
    std::vector<ReconstructionBinding>().swap(reconstruction_bindings_);
    std::vector<ComponentBinding>().swap(projected_components_);
    std::vector<ComponentBinding>().swap(private_original_components_);
    reconstructed_ = false;
    arrays_ = {}; previous_ = {};
    target_ = current_ = nullptr; battle_ = nullptr;
    bytes_ = 0;
    ready_ = write_complete_ = undo_started_ = false;
    executing_ = execution_settled_ = false;
    execution_reservation_ = previous_fingerprint_ = 0;
}

Status Sc6ReplayVfxState::ValidateHistoricalMaterials() const noexcept
{
    // Native1408D64E0/6620 append null entries for material slots without an
    // override. Native fade and unroot loops skip those entries. Array count
    // remains initialization/lifecycle state: retain exact current backing,
    // count, capacity and contents rather than erasing or reconstructing it.
    for(const auto& component:components_) {
        for(const auto& roots:component.material_roots) {
            if(roots.count<0 || roots.count>static_cast<int>(roots.entries.size()))
                return Status::failure(FailureCode::RestorePreflightFailed);
            for(int i=0;i<roots.count;++i)
                if(roots.entries[i].object || roots.entries[i].parent)
                    return Status::failure(FailureCode::UnsupportedContent);
        }
        if(component.lux) {
            const auto binding=CheckComponentBoundary(base_,component);
            if(!binding.ok())return binding;
        }
    }
    return Status::success();
}

Sc6ReplayVfxState::MaterialAdmissionWitness Sc6ReplayVfxState::HistoricalMaterialAdmission() const noexcept
{
    MaterialAdmissionWitness first{};
    for(const auto& component:components_)
        for(unsigned side=0;side<component.material_roots.size();++side) {
            const auto& roots=component.material_roots[side];
            if(!roots.count)continue;
            MaterialAdmissionWitness result{};result.component=component.address;
            result.array=side;result.count=roots.count;
            for(int i=0;i<roots.count && i<static_cast<int>(roots.entries.size());++i)
                if(const auto& entry=roots.entries[i];entry.object) {
                    if(!result.non_null) {result.first_material=entry.object;result.first_parent=entry.parent;}
                    ++result.non_null;
                }
            if(result.non_null)return result;
            if(!first.count)first=result;
        }
    return first;
}

Status Sc6ReplayVfxState::ProjectManagerSlot(std::size_t row,
    std::span<const ReconstructionBinding> bindings, ManagerSlotProjection& output) const noexcept
{
    if (row >= constructed_) return Status::failure(FailureCode::RestorePreflightFailed);
    output.bytes = slots_[row].bytes;
    const auto translate = [&](std::size_t offset) {
        const auto source = At<std::uintptr_t>(output.bytes.data(), offset);
        for (const auto& binding : bindings) if (binding.identity.source == source) {
            std::memcpy(output.bytes.data() + offset, &binding.identity.target, sizeof(source));
            break;
        }
    };
    translate(0);       // Native manager record actor.
    translate(0x68);    // Request+58 direct attachment, not a generic pointer scan.
    const auto count = At<int>(output.bytes.data(), 0xb0);
    const auto* provider = Provider(output.bytes.data() + 0x10);
    if (!count) return provider ? Status::failure(FailureCode::UnsupportedContent) : Status::success();
    if (count != 3 || !provider) return Status::failure(FailureCode::UnsupportedContent);
    if (!CopyBytes(output.provider.data(), provider, output.provider.size()))
        return Status::failure(FailureCode::ContextUnavailable);
    const auto weak = At<std::array<std::int32_t, 2>>(output.provider.data(), 8);
    for (const auto& binding : bindings) if (binding.identity.source_weak == weak) {
        std::memcpy(output.provider.data() + 8, binding.identity.target_weak.data(), 8);
        break;
    }
    const auto* projected = output.provider.data();
    std::memcpy(output.bytes.data() + 0xa0, &projected, sizeof(projected));
    return Status::success();
}

Status Sc6ReplayVfxState::ProjectManagerComponent(const ReconstructionBinding& binding,
    ComponentBinding& output) const noexcept
{
    const auto& identity = binding.identity;
    const ComponentBinding* source{};
    for (const auto& component : components_) if (component.address == identity.source) { source = &component; break; }
    if (!source || source->weak != identity.source_weak || !output.emitters.empty())
        return Status::failure(FailureCode::GenerationMismatch);
    output.address = identity.target; output.weak = identity.target_weak;
    auto status = ReadComponentValues(base_, output);
    if (!status.ok()) return status;
    if (source->tick_entry != output.tick_entry || source->particle_template != output.particle_template
        || source->attach_parent != output.attach_parent || source->attach_socket != output.attach_socket
        || source->lux != output.lux || source->completions != output.completions || source->events != output.events)
        return Status::failure(FailureCode::GenerationMismatch);
    for (std::size_t i = 0; i < source->material_roots.size(); ++i) {
        const auto& old = source->material_roots[i]; const auto& fresh = output.material_roots[i];
        if (old.count < 0 || old.count > static_cast<int>(old.entries.size()) || fresh.count != old.count)
            return Status::failure(FailureCode::RestorePreflightFailed);
        for (int j = 0; j < old.count; ++j)
            if (old.entries[j].object || old.entries[j].parent || fresh.entries[j].object || fresh.entries[j].parent)
                return Status::failure(FailureCode::UnsupportedContent);
        if (identity.source == identity.target) {
            if (old != fresh) return Status::failure(FailureCode::GenerationMismatch);
        } else if (old.data && old.data == fresh.data)
            return Status::failure(FailureCode::GenerationMismatch);
    }
    // Retain the new owner's physical material backing. Only these value bytes
    // come from A; neither its array headers nor its retired UObject are read.
    output.values = source->values; output.active_flags = source->active_flags;
    return Status::success();
}

Status Sc6ReplayVfxState::PrepareManager(const Sc6ReplayVfxState& current, void* battle,
    std::size_t budget, PreparedManager& output, std::span<const ReconstructionBinding> bindings) const noexcept
{
    if (output.target_ || output.ready_ || output.published_) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || base_ != current.base_ || manager_ != current.manager_ || manager_weak_ != current.manager_weak_
        || thread_ != GetCurrentThreadId()
        || (bindings.empty() && (!object_lease_ || !object_lease_->Validate().ok())))
        return Status::failure(FailureCode::GenerationMismatch);
    const auto materials=bindings.empty() ? ValidateHistoricalMaterials() : ValidateReconstructionBindings(bindings);
    if(!materials.ok())return materials;
    auto status = current.ValidateHeld(base_, battle);
    if (!status.ok()) return status;
    const auto images = owned_bytes() + (this == &current ? 0 : current.owned_bytes());
    if (images > budget || sizeof(output) > budget - images) return Status::failure(FailureCode::CapacityExceeded);
    const auto allowance = budget - images;
    try
    {
        output.target_ = this; output.current_ = &current; output.battle_ = battle;
        output.bytes_ = sizeof(output);
        if (!bindings.empty()) {
            const auto unit = sizeof(ReconstructionBinding) + 2 * sizeof(ComponentBinding);
            if (bindings.size() > (allowance - output.bytes_) / unit)
            { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
            output.reconstructed_ = true;
            output.reconstruction_bindings_.assign(bindings.begin(), bindings.end());
            output.projected_components_.resize(bindings.size());
            output.private_original_components_.reserve(bindings.size());
            const auto charge = output.reconstruction_bindings_.capacity() * sizeof(ReconstructionBinding)
                + output.projected_components_.capacity() * sizeof(ComponentBinding)
                + output.private_original_components_.capacity() * sizeof(ComponentBinding);
            if (charge > allowance - output.bytes_)
            { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
            output.bytes_ += charge;
            for (std::size_t i = 0; i < bindings.size(); ++i) {
                const auto& identity = bindings[i].identity;
                if (identity.source != identity.target) {
                    for (const auto& owner : current.components_) if (owner.address == identity.target)
                    { output.Clear(); return Status::failure(FailureCode::GenerationMismatch); }
                    // Manager listeners are independently owned logical receivers.
                    // A component receiver needs an explicit callback participant.
                    for (std::size_t n = 0; n < tables_[0].count; ++n)
                        if (At<std::array<std::int32_t,2>>(tables_[0].bytes.get(), n * 16) == identity.source_weak)
                        { output.Clear(); return Status::failure(FailureCode::UnsupportedContent); }
                }
                if(identity.source!=identity.target) {
                    auto& private_image=output.private_original_components_.emplace_back();
                    private_image.address=identity.target;private_image.weak=identity.target_weak;
                    status=ReadComponentValues(base_,private_image);
                    if(!status.ok()) {output.Clear();return status;}
                }
                status = ProjectManagerComponent(bindings[i], output.projected_components_[i]);
                if (!status.ok()) { output.Clear(); return status; }
            }
        }
        if (output.bytes_ > allowance || 8 * sizeof(PreparedManager::Allocation) > allowance - output.bytes_)
        { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        output.allocations_.reserve(8);
        output.bytes_ += output.allocations_.capacity() * sizeof(PreparedManager::Allocation);
        if (output.bytes_ > allowance || (constructed_ + current.constructed_) > (allowance - output.bytes_) / sizeof(void*))
        { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        output.provider_bindings_[0].reserve(current.constructed_);
        output.provider_bindings_[1].reserve(constructed_);
        for (const auto& bindings : output.provider_bindings_) output.bytes_ += bindings.capacity() * sizeof(void*);
        if (!CopyBytes(&output.epoch_, reinterpret_cast<void*>(base_ + 0x4197170), 8)
            || !CopyBytes(output.previous_map_.data(), reinterpret_cast<void*>(manager_ + 0x408), 0x50))
        { output.Clear(); return Status::failure(FailureCode::ContextUnavailable); }
        output.map_ = definition_layout_;
        const auto allocate = [&](std::size_t requested, const void* source, std::size_t copied,
            void* previous, std::size_t previous_bytes, void*& destination) -> Status {
            if (copied > requested || bool(previous) != bool(previous_bytes))
                return Status::failure(FailureCode::RestorePreflightFailed);
            const auto quantify = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base_ + 0xd50dc0);
            const auto charge = requested ? quantify(requested, 0) : 0;
            const auto old_charge = previous_bytes ? quantify(previous_bytes, 0) : 0;
            if (charge < requested || old_charge < previous_bytes || output.bytes_ > allowance
                || charge > allowance - output.bytes_ || old_charge > allowance - output.bytes_ - charge)
                return Status::failure(FailureCode::CapacityExceeded);
            if (output.allocations_.size() == output.allocations_.capacity())
                return Status::failure(FailureCode::CapacityExceeded);
            output.allocations_.push_back({nullptr, previous, requested, previous_bytes});
            auto& allocation = output.allocations_.back();
            if (requested)
            {
                allocation.target = reinterpret_cast<void* (*)(std::size_t)>(base_ + 0x4a61c0)(requested);
                if (!allocation.target) return Status::failure(FailureCode::CapacityExceeded);
                std::memset(allocation.target, 0, requested);
                if (copied && !CopyBytes(allocation.target, source, copied))
                    return Status::failure(FailureCode::ContextUnavailable);
            }
            destination = allocation.target;
            output.bytes_ += charge + old_charge;
            return Status::success();
        };
        std::size_t row = 0, target_provider_bytes = 0, previous_provider_bytes = 0;
        for (std::size_t a = 0; a < output.arrays_.size(); ++a)
        {
            const auto offset = a < 2 ? 0x3e8 + a * 0x10 : table_offsets[a - 2];
            auto& prior = output.previous_[a];
            auto& target = output.arrays_[a];
            if (!CopyBytes(&prior, reinterpret_cast<void*>(manager_ + offset), sizeof(prior))
                || prior.count < 0 || prior.capacity < prior.count || (prior.capacity && !prior.data))
            { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            const auto count = a < 2 ? counts_[a] : static_cast<int>(tables_[a - 2].count);
            const auto capacity = a < 2 ? slot_capacities_[a] : tables_[a - 2].native_capacity;
            const auto stride = a < 2 ? 0xc0 : table_strides[a - 2];
            if (count < 0 || capacity < count)
            { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            status = allocate(static_cast<std::size_t>(capacity) * stride,
                a < 2 ? nullptr : tables_[a - 2].bytes.get(), a < 2 ? 0 : static_cast<std::size_t>(count) * stride,
                prior.data, static_cast<std::size_t>(prior.capacity) * stride, target.data);
            if (!status.ok()) { output.Clear(); return status; }
            target.capacity = capacity;
            if (a >= 2) { target.count = count; continue; }
            for (int i = 0; i < prior.count; ++i)
            {
                output.provider_bindings_[0].push_back(Provider(static_cast<const std::byte*>(prior.data) + i * 0xc0 + 0x10));
                status = ValidateProvider(base_, static_cast<const std::byte*>(prior.data) + i * 0xc0 + 0x10,
                    previous_provider_bytes);
                if (!status.ok()) { output.Clear(); return status; }
            }
            for (int i = 0; i < count; ++i, ++row)
            {
                ManagerSlotProjection captured;
                status = ProjectManagerSlot(row, bindings, captured);
                if (!status.ok()) { output.Clear(); return status; }
                status = ValidateProvider(base_, captured.bytes.data() + 0x10, target_provider_bytes);
                if (!status.ok() || output.bytes_ > allowance
                    || target_provider_bytes > allowance - output.bytes_
                    || previous_provider_bytes > allowance - output.bytes_ - target_provider_bytes)
                { output.Clear(); return status.ok() ? Status::failure(FailureCode::CapacityExceeded) : status; }
                auto* slot = static_cast<std::byte*>(target.data) + i * 0xc0;
                std::memcpy(slot, captured.bytes.data(), 12);
                reinterpret_cast<void* (*)(void*, const void*)>(base_ + 0x3add60)(slot + 0x10, captured.bytes.data() + 0x10);
                ++target.count;
                output.provider_bindings_[1].push_back(Provider(slot + 0x10));
                if (!SameRequest(base_, captured.bytes.data() + 0x10, slot + 0x10))
                { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            }
        }
        if (output.bytes_ > allowance || target_provider_bytes > allowance - output.bytes_
            || previous_provider_bytes > allowance - output.bytes_ - target_provider_bytes)
        { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        output.bytes_ += target_provider_bytes + previous_provider_bytes;
        constexpr std::array<std::size_t, 3> pointers{0, 0x20, 0x40};
        const auto* original = definition_layout_.data();
        for (std::size_t i = 0; i < pointers.size(); ++i)
        {
            const auto p = pointers[i];
            const bool heap = i == 0 || At<std::uintptr_t>(original, p) != 0;
            const bool old_heap = i == 0 || At<std::uintptr_t>(output.previous_map_.data(), p) != 0;
            const auto extent = [&](const std::byte* h) -> std::size_t {
                if (i == 0) return static_cast<std::size_t>(At<int>(h, 0xc)) * 32;
                if (i == 1) return ((static_cast<std::size_t>(At<int>(h, 0x2c)) + 31) / 32) * 4;
                return static_cast<std::size_t>(At<int>(h, 0x48)) * 4;
            };
            if (!heap && !old_heap) continue;
            void* pointer{};
            status = allocate(heap ? extent(original) : 0, definition_storage_[i].bytes.get(),
                heap ? definition_storage_[i].count * definition_strides[i] : 0,
                old_heap ? At<void*>(output.previous_map_.data(), p) : nullptr,
                old_heap ? extent(output.previous_map_.data()) : 0, pointer);
            if (!status.ok()) { output.Clear(); return status; }
            std::memcpy(output.map_.data() + p, &pointer, sizeof(pointer));
        }
        // Native arrays and per-slot providers must have exclusive ownership.
        // Reject overlapping old allocations before any retirement can occur.
        for (std::size_t i = 0; i < output.allocations_.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto& a = output.allocations_[i]; const auto& b = output.allocations_[j];
                const auto x = reinterpret_cast<std::uintptr_t>(a.previous), y = reinterpret_cast<std::uintptr_t>(b.previous);
                if (a.previous_bytes && b.previous_bytes && (a.previous_bytes > UINTPTR_MAX - x
                    || b.previous_bytes > UINTPTR_MAX - y || (x < y + b.previous_bytes && y < x + a.previous_bytes)))
                { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            }
        for (std::size_t a = 0; a < 2; ++a)
            for (int i = 0; i < output.previous_[a].count; ++i)
            {
                const auto* request = static_cast<const std::byte*>(output.previous_[a].data) + i * 0xc0 + 0x10;
                const auto* pointer = Provider(request);
                if (!pointer) continue;
                for (std::size_t b = 0; b <= a; ++b)
                    for (int j = 0; j < (b == a ? i : output.previous_[b].count); ++j)
                        if (pointer == Provider(static_cast<const std::byte*>(output.previous_[b].data) + j * 0xc0 + 0x10))
                        { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            }
        // Validate exclusive allocation ownership before any participant can
        // publish. An interior alias is just as unsafe to retire as an equal
        // base pointer. The admitted provider owns 48 native bytes.
        const auto header_overlap = [&](std::uintptr_t pointer, std::size_t bytes) {
            if (Overlaps(pointer, bytes, manager_ + 0x3e0, 4)
                || Overlaps(pointer, bytes, manager_ + 0x408, 0x50)) return true;
            for (std::size_t a = 0; a < output.arrays_.size(); ++a)
                if (Overlaps(pointer, bytes, manager_ + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2]),
                    sizeof(PreparedManager::Array))) return true;
            return false;
        };
        for (const auto& allocation : output.allocations_)
            if (header_overlap(reinterpret_cast<std::uintptr_t>(allocation.previous), allocation.previous_bytes)
                || header_overlap(reinterpret_cast<std::uintptr_t>(allocation.target), allocation.target_bytes))
            { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
        for (std::size_t image = 0; image < output.provider_bindings_.size(); ++image)
            for (std::size_t i = 0; i < output.provider_bindings_[image].size(); ++i)
            {
                const auto pointer = reinterpret_cast<std::uintptr_t>(output.provider_bindings_[image][i]);
                if (!pointer) continue;
                if (header_overlap(pointer, 48))
                { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
                for (const auto& allocation : output.allocations_)
                    if (Overlaps(pointer, 48, reinterpret_cast<std::uintptr_t>(allocation.previous), allocation.previous_bytes)
                        || Overlaps(pointer, 48, reinterpret_cast<std::uintptr_t>(allocation.target), allocation.target_bytes))
                    { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
                for (std::size_t other = 0; other <= image; ++other)
                    for (std::size_t j = 0; j < (other == image ? i : output.provider_bindings_[other].size()); ++j)
                        if (output.provider_bindings_[other][j] && Overlaps(pointer, 48,
                            reinterpret_cast<std::uintptr_t>(output.provider_bindings_[other][j]), 48))
                        { output.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
            }
        output.ready_ = true;
        status = output.ValidateImage(false);
        if (status.ok()) status = output.ValidatePayload(true);
        if (status.ok()) status = output.ValidateComponents(true, false);
        if (!status.ok()) output.Clear();
        return status;
    }
    catch (...) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplayVfxState::PreparedManager::ReadFinishDispatchBinding(ReplayVfxFinishBinding& output) const noexcept
{
    output={};
    if(!ready_ || !published_ || !write_complete_ || undo_started_ || execution_settled_
        || !target_ || !current_ || !target_->valid_ || !current_->valid_
        || target_->base_!=current_->base_ || target_->manager_!=current_->manager_
        || target_->manager_weak_!=current_->manager_weak_ || target_->thread_!=GetCurrentThreadId())
        return Status::failure(FailureCode::UnsupportedContent);
    const auto& table=target_->tables_[0];const auto& array=arrays_[2];
    if(table.count>2 || (table.count && !table.bytes) || array.count<0
        || static_cast<std::size_t>(array.count)!=table.count || array.capacity!=table.native_capacity)
        return Status::failure(FailureCode::UnsupportedContent);
    output.base=target_->base_;output.manager=target_->manager_;output.manager_weak=target_->manager_weak_;
    output.epoch=epoch_;output.header={reinterpret_cast<std::uintptr_t>(array.data),array.count,array.capacity};
    // Use A's admitted bytes; never the current live allocation as a baseline.
    if(table.count)std::memcpy(output.rows.data(),table.bytes.get(),table.count*16);
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedManager::ValidateBinding() const noexcept
{
    if (!ready_ || !target_ || !current_ || target_->thread_ != GetCurrentThreadId()
        || !target_->valid_ || !current_->valid_ || target_->base_ != current_->base_
        || target_->manager_ != current_->manager_ || !target_->object_lease_ || !current_->object_lease_
        || !(reconstructed_ && !execution_settled_ ? target_->ValidateReconstructionBindings(reconstruction_bindings_).ok()
            : target_->object_lease_->Validate().ok()) || !current_->object_lease_->Validate().ok())
        return Status::failure(FailureCode::GenerationMismatch);
    std::uint64_t epoch{};
    std::uintptr_t manager{};
    if (!CopyBytes(&epoch, reinterpret_cast<void*>(target_->base_ + 0x4197170), 8) || epoch != epoch_
        || !CopyBytes(&manager, static_cast<const std::byte*>(battle_) + 0x508, sizeof(manager)) || manager != target_->manager_
        || !Live(target_->base_, target_->manager_weak_, manager)
        || !target_->ValidateEmptyNormalizer().ok()) return Status::failure(FailureCode::GenerationMismatch);
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedManager::ValidatePayload(bool target) const noexcept
{
    const auto* image = target ? target_ : current_;
    const auto& arrays = target ? arrays_ : previous_;
    const auto& map = target ? map_ : previous_map_;
    __try
    {
        std::size_t row = 0, provider_bytes = 0;
        for (std::size_t a = 0; a < arrays.size(); ++a)
        {
            if (a < 2)
            {
                if (arrays[a].count != image->counts_[a]) return Status::failure(FailureCode::RestoreVerificationFailed);
                for (int i = 0; i < arrays[a].count; ++i, ++row)
                {
                    const auto* slot = static_cast<const std::byte*>(arrays[a].data) + i * 0xc0;
                    ManagerSlotProjection expected;
                    const auto projection = image->ProjectManagerSlot(row,
                        target && reconstructed_ && !execution_settled_ ? std::span<const ReconstructionBinding>(reconstruction_bindings_) : std::span<const ReconstructionBinding>{}, expected);
                    if (!projection.ok()) return projection;
                    if (row >= provider_bindings_[target ? 1 : 0].size()
                        || Provider(slot + 0x10) != provider_bindings_[target ? 1 : 0][row]
                        || !SameIdentity(slot, expected.bytes.data())
                        || !ValidateProvider(image->base_, slot + 0x10, provider_bytes).ok()
                        || !SameRequest(image->base_, slot + 0x10, expected.bytes.data() + 0x10))
                        return Status::failure(FailureCode::RestoreVerificationFailed);
                }
            }
            else
            {
                const auto& table = image->tables_[a - 2];
                if (arrays[a].count < 0 || static_cast<std::size_t>(arrays[a].count) != table.count)
                    return Status::failure(FailureCode::RestoreVerificationFailed);
                if (a == 3)
                {
                    for (std::size_t i = 0; i < table.count; ++i)
                    {
                        const auto* bytes = static_cast<const std::byte*>(arrays[a].data) + i * 12;
                        if (bytes[0] != table.bytes[i * 12] || std::memcmp(bytes + 4, table.bytes.get() + i * 12 + 4, 8))
                            return Status::failure(FailureCode::RestoreVerificationFailed);
                    }
                }
                else if (table.count && std::memcmp(arrays[a].data, table.bytes.get(), table.count * table_strides[a - 2]))
                    return Status::failure(FailureCode::RestoreVerificationFailed);
            }
        }
        const auto* h = map.data();
        const auto flags = At<std::uintptr_t>(h, 0x20), hashes = At<std::uintptr_t>(h, 0x40);
        const std::array<const void*, 3> data{At<const void*>(h, 0), flags ? reinterpret_cast<const void*>(flags) : h + 0x10,
            hashes ? reinterpret_cast<const void*>(hashes) : h + 0x38};
        for (std::size_t i = 0; i < data.size(); ++i)
            if (image->definition_storage_[i].count && std::memcmp(data[i], image->definition_storage_[i].bytes.get(),
                image->definition_storage_[i].count * definition_strides[i])) return Status::failure(FailureCode::RestoreVerificationFailed);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::PreparedManager::ValidateComponents(bool target, bool compare_values) const noexcept
{
    if (!target_ || !current_) return Status::failure(FailureCode::IllegalTransition);
    __try
    {
        for(unsigned pass=0;pass<2;++pass) {
        if(pass && (target || !reconstructed_ || executing_))break;
        const auto& components=pass?private_original_components_:
            (reconstructed_ ? (target ? (execution_settled_ ? target_->components_ : projected_components_) : current_->components_) : target_->components_);
        for (const auto& a : components)
        {
            const ComponentBinding* b = reconstructed_ ? &a : nullptr;
            if (!reconstructed_) for (const auto& candidate : current_->components_)
                if (candidate.address == a.address) { b = &candidate; break; }
            // A successful execution may create owners absent from B. Their
            // current C binding was captured and leased during settlement;
            // they remain native-owned on commit. Undo cannot use this case.
            if(!b && target && executing_ && execution_settled_
                && execution_settlement_==RestoreSettlement::CommitCurrent) b=&a;
            if (!b || b->weak != a.weak || b->tick_entry != a.tick_entry
                || b->attach_parent != a.attach_parent || b->attach_socket != a.attach_socket
                || b->particle_template != a.particle_template || b->lux != a.lux)
                return Status::failure(FailureCode::GenerationMismatch);
            if(a.material_roots!=b->material_roots || a.completions!=b->completions || a.events!=b->events)
                return Status::failure(FailureCode::GenerationMismatch);
            auto status = CheckComponentBoundary(target_->base_, *b);
            if (!status.ok()) return status;
            const auto& image = target ? a : *b;
            const auto* object = reinterpret_cast<const void*>(image.address);
            if (compare_values && (At<std::uint32_t>(object, 0x188) & 0x60000) != image.active_flags)
                return Status::failure(FailureCode::RestoreVerificationFailed);
            std::size_t cursor = 0;
            for (const auto span : component_spans)
            {
                if (span.lux && !image.lux) continue;
                if (span.bytes > image.values.size() - cursor)
                    return Status::failure(FailureCode::RestorePreflightFailed);
                if (compare_values && std::memcmp(static_cast<const std::byte*>(object) + span.offset,
                    image.values.data() + cursor, span.bytes)) return Status::failure(FailureCode::RestoreVerificationFailed);
                cursor += span.bytes;
            }
        }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayVfxState::PreparedManager::ValidateImage(bool target) const noexcept
{
    auto status = ValidateBinding();
    if (!status.ok()) return status;
    const auto& arrays = target ? arrays_ : previous_;
    const auto& map = target ? map_ : previous_map_;
    const auto* image = target ? target_ : current_;
    __try
    {
        if (At<int>(reinterpret_cast<void*>(image->manager_), 0x3e0) != image->sequence_
            || std::memcmp(reinterpret_cast<void*>(image->manager_ + 0x408), map.data(), map.size()))
            return Status::failure(FailureCode::RestoreVerificationFailed);
        for (std::size_t a = 0; a < arrays.size(); ++a)
            if (std::memcmp(reinterpret_cast<void*>(image->manager_ + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2])),
                &arrays[a], sizeof(Array))) return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
    status = ValidatePayload(target);
    return status.ok() ? ValidateComponents(target, true) : status;
}

bool Sc6ReplayVfxState::PreparedManager::Write(bool target) noexcept
{
    const auto& arrays = target ? arrays_ : previous_;
    const auto& map = target ? map_ : previous_map_;
    const auto* image = target ? target_ : current_;
    __try
    {
        for (std::size_t a = 0; a < arrays.size(); ++a)
            std::memcpy(reinterpret_cast<void*>(image->manager_ + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2])),
                &arrays[a], sizeof(Array));
        std::memcpy(reinterpret_cast<void*>(image->manager_ + 0x408), map.data(), map.size());
        std::memcpy(reinterpret_cast<void*>(image->manager_ + 0x3e0), &image->sequence_, sizeof(image->sequence_));
        for(unsigned pass=0;pass<2;++pass) {
        if(pass && (target || !reconstructed_ || executing_))break;
        const auto& components=pass?private_original_components_:
            (reconstructed_ ? (target ? (execution_settled_ ? target_->components_ : projected_components_) : current_->components_) : target_->components_);
        for (const auto& a : components)
        {
            const ComponentBinding* values = &a;
            if (!target && !reconstructed_)
            {
                values = nullptr;
                for (const auto& b : current_->components_)
                    if (b.address == a.address) { values = &b; break; }
                if (!values) return false;
            }
            auto* object = reinterpret_cast<std::byte*>(values->address);
            const auto flags = (At<std::uint32_t>(object, 0x188) & ~0x60000u) | values->active_flags;
            std::memcpy(object + 0x188, &flags, sizeof(flags));
            std::size_t cursor = 0;
            for (const auto span : component_spans)
            {
                if (span.lux && !values->lux) continue;
                std::memcpy(object + span.offset, values->values.data() + cursor, span.bytes);
                cursor += span.bytes;
            }
        }
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayVfxState::PreparedManager::Publish() noexcept
{
    if (published_) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateImage(false);
    if (status.ok()) status = ValidatePayload(true);
    if (status.ok()) status = ValidateComponents(true, false);
    if (!status.ok()) return status;
    // Preflight every destination, including the exact B undo destinations.
    // SEH below still handles a protection/lifetime change after admission.
    for (std::size_t a = 0; a < arrays_.size(); ++a)
        if (!WritableRange(target_->manager_ + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2]), sizeof(Array)))
            return Status::failure(FailureCode::RestorePreflightFailed);
    if (!WritableRange(target_->manager_ + 0x408, map_.size())
        || !WritableRange(target_->manager_ + 0x3e0, sizeof(target_->sequence_)))
        return Status::failure(FailureCode::RestorePreflightFailed);
    for (const auto& component : (reconstructed_ ? projected_components_ : target_->components_))
    {
        if (!WritableRange(component.address + 0x188, 4)) return Status::failure(FailureCode::RestorePreflightFailed);
        for (const auto span : component_spans)
            if ((!span.lux || component.lux) && !WritableRange(component.address + span.offset, span.bytes))
                return Status::failure(FailureCode::RestorePreflightFailed);
    }
    if (reconstructed_) for (const auto& component : current_->components_) {
        if (!WritableRange(component.address + 0x188, 4)) return Status::failure(FailureCode::RestorePreflightFailed);
        for (const auto span : component_spans)
            if ((!span.lux || component.lux) && !WritableRange(component.address + span.offset, span.bytes))
                return Status::failure(FailureCode::RestorePreflightFailed);
    }
    published_ = true;
    write_complete_ = Write(true);
    status = write_complete_ ? ValidatePublished() : Status::failure(FailureCode::RestoreWriteFailed);
    if (!status.ok() && !Undo().ok()) return Status::failure(FailureCode::UndoFailed);
    return status;
}

Status Sc6ReplayVfxState::PreparedManager::ValidatePublished() const noexcept
{
    if (executing_ && !execution_settled_) return Status::failure(FailureCode::IllegalTransition);
    return published_ && !undo_started_ ? ValidateImage(true) : Status::failure(FailureCode::IllegalTransition);
}

Status Sc6ReplayVfxState::ParticleBirthSet::ValidateTraceAttachment(int slot_id,std::uintptr_t attachment) const noexcept
{
    if(slot_id<0)return Status::success();
    if(empty())return Status::failure(FailureCode::RestorePreflightFailed);
    auto status=ValidateCurrentB();if(!status.ok())return status;
    __try {
        const auto& current=*members_[0].current_;
        unsigned matches{};
        for(std::size_t i=0;i<current.constructed_;++i) {
            const auto& row=current.slots_[i];
            const auto owner=At<std::uintptr_t>(row.bytes.data(),0);
            const bool retiring=std::any_of(members().begin(),members().end(),[&](const auto& birth){return birth.component_==owner;});
            if(At<int>(row.bytes.data(),8)==slot_id) {
                ++matches;if(!owner || !retiring)return Status::failure(FailureCode::RestorePreflightFailed);
            }
            const auto* provider=Provider(row.bytes.data()+0x10);
            if(attachment && provider && reinterpret_cast<std::uintptr_t(*)(const void*)>(current.base_+0xf823f0)(provider+8)==attachment
                && !retiring)return Status::failure(FailureCode::RestorePreflightFailed);
        }
        return matches==1?Status::success():Status::failure(FailureCode::RestorePreflightFailed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayVfxState::ParticleBirthSet::RetireCurrentForUndo(
    const Sc6ReplayVfxState& protected_b, RecoveryRetirement& progress) const noexcept
{
    if(progress.failed || progress.completed>count_ || !protected_b.retained_owners_live())
        return Status::failure(FailureCode::GenerationMismatch);
    if(!progress.admitted) {
        if(progress.completed || progress.slot_removed) return Status::failure(FailureCode::IllegalTransition);
        // Complete preflight before the first irreversible C-only teardown.
        // The existing retirement contract rejects physics, nav/overlap,
        // attachment descendants, material roots and live callback payloads.
        for(const auto& birth:members()) {
            if(birth.current_->base_!=protected_b.base_ || birth.current_->manager_!=protected_b.manager_)
                return Status::failure(FailureCode::GenerationMismatch);
            for(const auto& b:protected_b.components_) if(b.address==birth.component_)
                return Status::failure(FailureCode::RestorePreflightFailed);
            auto status=birth.ValidateCurrentB();if(status.ok()) status=birth.ValidateRetirement();
            if(!status.ok()) return status;
        }
        progress.admitted=true;
    }
    __try {
        while(progress.completed<count_) {
            const auto& birth=members_[progress.completed];
            const auto base=birth.current_->base_;
            if(At<std::uint64_t>(reinterpret_cast<void*>(base),0x4197170)!=birth.epoch_
                || !Live(base,birth.weak_,birth.component_)
                || !Live(base,birth.current_->manager_weak_,birth.current_->manager_))
                return Status::failure(FailureCode::GenerationMismatch);
            if(!progress.slot_removed) {
                // Recovery disposal is not an authored DestroyParticle event.
                // Use its native provider-compaction callee without emitting
                // a new manager delegate into the simulation being discarded.
                // The component destructor's callback contract was checked above.
                const auto* array=reinterpret_cast<void*>(birth.current_->manager_+0x3e8);
                const auto count=At<int>(array,8),capacity=At<int>(array,12);
                const auto* data=At<const std::byte*>(array,0);
                if(count<1 || count>4096 || capacity<count || !data)
                    return Status::failure(FailureCode::GenerationMismatch);
                unsigned matches{};
                for(int i=0;i<count;++i) matches+=At<std::uintptr_t>(data+std::size_t(i)*0xc0,0)==birth.component_;
                if(matches!=1) return Status::failure(FailureCode::GenerationMismatch);
                auto* component=reinterpret_cast<void*>(birth.component_);
                const auto removed=reinterpret_cast<int(*)(void*,void**)>(base+0x85c8a0)(
                    const_cast<void*>(array),&component);
                if(removed!=1 || At<int>(array,8)!=count-1) {
                    progress.failed=true;return Status::failure(FailureCode::RestoreVerificationFailed);
                }
                progress.slot_removed=true;
            }
            // Unlike displaced B retirement, C's tiles still belong to the
            // installed native pool. Preserve their count so native teardown
            // returns them. Zeroing it would lose tiles before B settlement.
            reinterpret_cast<void(*)(void*,bool)>(base+0x8cecf0)(reinterpret_cast<void*>(birth.component_),false);
            const auto* component=reinterpret_cast<void*>(birth.component_);
            if((At<unsigned>(component,0x188)&7) || At<int>(component,0xa58)
                || At<std::uintptr_t>(component,0xa70) || Live(base,birth.weak_,birth.component_)) {
                progress.failed=true;return Status::failure(FailureCode::RestoreVerificationFailed);
            }
            ++progress.completed;progress.slot_removed=false;
        }
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        progress.failed=true;return Status::failure(FailureCode::RestoreWriteFailed);
    }
}

Status Sc6ReplayVfxState::ParticleBirth::ValidateQuarantined(const char** failed_check) const noexcept
{
    if(failed_check)*failed_check="lifetime";
    auto status=ValidateLifetime();if(!status.ok()) return status;
    __try {
        if(failed_check)*failed_check="captured_component";
        const ComponentBinding* saved=nullptr;
        for(const auto& row:current_->components_) if(row.address==component_) {saved=&row;break;}
        if(!saved || saved->weak!=weak_) return Status::failure(FailureCode::GenerationMismatch);
        if(failed_check)*failed_check="component_boundary";
        status=CheckComponentBoundary(current_->base_,*saved);if(!status.ok()) return status;
        const auto* component=reinterpret_cast<const std::byte*>(component_);
        // Scheduler exclusion, unattached ownership and unchanged component
        // state are all required. A retained UObject alone is not quarantine.
        if(failed_check)*failed_check="quarantine_admission";
        if((At<unsigned char>(component,0x110+0xc)&0x40) || !(At<unsigned>(component,0x188)&1)
            || saved->attach_parent || At<std::uintptr_t>(component,0x1d0)
            || (At<unsigned>(component,0x188)&0x60000)!=saved->active_flags)
            return Status::failure(FailureCode::GenerationMismatch);
        if(failed_check)*failed_check="component_values";
        std::size_t cursor{};
        for(const auto span:component_spans) {
            if(span.lux && !saved->lux) continue;
            if(cursor>saved->values.size() || span.bytes>saved->values.size()-cursor
                || std::memcmp(component+span.offset,saved->values.data()+cursor,span.bytes))
                return Status::failure(FailureCode::RestoreVerificationFailed);
            cursor+=span.bytes;
        }
        if(failed_check)*failed_check="cpu_emitter_graph";
        for(const auto& cpu:current_->cpu_emitters_) if(cpu.component==component_) {
            if(cpu.ordinal>=saved->emitters.size()) return Status::failure(FailureCode::GenerationMismatch);
            status=cpu.image.ValidateNativeGraph(reinterpret_cast<void*>(saved->emitters[cpu.ordinal]));
            if(!status.ok()) return status;
        }
        if(failed_check)*failed_check="gpu_emitter_graph";
        for(const auto& gpu:current_->gpu_owners_) if(gpu.component==component_) {
            status=gpu.image.ValidateNativeGraph(reinterpret_cast<void*>(gpu.emitter));
            if(!status.ok()) return status;
            if(At<int>(reinterpret_cast<void*>(gpu.render_storage),0x240)!=-1)
                return Status::failure(FailureCode::RestoreVerificationFailed);
        }
        if(failed_check)*failed_check=nullptr;
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

bool Sc6ReplayVfxState::PreparedManager::PreviousFingerprint(std::uint64_t& result) const noexcept
{
    __try {
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto& allocation : allocations_)
            for (std::size_t i = 0; i < allocation.previous_bytes; ++i)
                hash = (hash ^ static_cast<const unsigned char*>(allocation.previous)[i]) * 1099511628211ull;
        for (const auto* provider : provider_bindings_[0])
            if (provider) for (std::size_t i = 0; i < 48; ++i)
                hash = (hash ^ static_cast<unsigned char>(provider[i])) * 1099511628211ull;
        result = hash;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayVfxState::PreparedManager::BeginExecution(std::size_t retirement_budget) noexcept
{
    if (executing_ || !published_ || !write_complete_ || undo_started_)
        return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidatePublished();
    if (status.ok()) status = ValidatePayload(false);
    if (!status.ok()) return status;
    if (retirement_budget < bytes_ || retirement_budget > SIZE_MAX - bytes_)
        return Status::failure(FailureCode::CapacityExceeded);
    if (!PreviousFingerprint(previous_fingerprint_)) return Status::failure(FailureCode::ContextUnavailable);
    // Native code now owns A, including provider destruction on slot removal.
    // Never dereference or free these addresses after execution admission.
    for (auto& allocation : allocations_) { allocation.target = nullptr; allocation.target_bytes = 0; }
    arrays_ = {}; map_ = {}; provider_bindings_[1].clear();
    execution_reservation_ = retirement_budget;
    bytes_ += retirement_budget;
    executing_ = true;
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedManager::SettleExecution(const Sc6ReplayVfxState& observed, RestoreSettlement purpose,
    const PreparedEmitterSet* reconstructed,const PreparedEmitterSet* reconstructed_cpu) noexcept
{
    if (!executing_ || execution_settled_ || undo_started_ || !published_ || !target_ || !current_)
        return Status::failure(FailureCode::IllegalTransition);
    if(purpose!=RestoreSettlement::RecoverOriginal && purpose!=RestoreSettlement::CommitCurrent)
        return Status::failure(FailureCode::IllegalTransition);
    if (reconstructed_ && !reconstructed) return Status::failure(FailureCode::GenerationMismatch);
    if (observed.base_ != target_->base_ || observed.manager_ != target_->manager_
        || observed.manager_weak_ != target_->manager_weak_ || !current_->retained_owners_live())
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = observed.ValidateHeld(observed.base_, battle_);
    if (!status.ok()) return status;
    if (reconstructed_) {
        for (const auto& old : projected_components_) {
            const auto found = std::find_if(observed.components_.begin(), observed.components_.end(),
                [&](const auto& c) { return c.address == old.address && c.weak == old.weak; });
            if (found == observed.components_.end()) {
                const auto gpu=reconstructed->FreshComponentRoots(old.address);
                const auto cpu=reconstructed_cpu?reconstructed_cpu->FreshComponentRoots(old.address):0;
                if ((!gpu && !cpu) || (gpu && !reconstructed->FreshComponentDeath(old.address,old.weak))
                    || (cpu && !reconstructed_cpu->FreshComponentDeath(old.address,old.weak)))
                    return Status::failure(FailureCode::GenerationMismatch);
            } else if (found->particle_template != old.particle_template || found->tick_entry != old.tick_entry
                || found->attach_parent != old.attach_parent || found->attach_socket != old.attach_socket || found->lux != old.lux)
                return Status::failure(FailureCode::GenerationMismatch);
        }
        if (purpose == RestoreSettlement::RecoverOriginal) for (const auto& owner : observed.components_)
            if (std::none_of(current_->components_.begin(), current_->components_.end(),
                [&](const auto& b) { return b.address == owner.address && b.weak == owner.weak; }))
                return Status::failure(FailureCode::GenerationMismatch);
    } else {
    // Recovery settles after C-only teardown; commit preserves additional C
    // owners. Every original A owner must survive in either case.
    if (observed.components_.size() < target_->components_.size()
        || (purpose == RestoreSettlement::RecoverOriginal && observed.components_.size() != target_->components_.size()))
        return Status::failure(FailureCode::GenerationMismatch);
    for (const auto& old : target_->components_) {
        const ComponentBinding* found = nullptr;
        for (const auto& c : observed.components_) if (c.address == old.address) { found = &c; break; }
        if (!found || found->weak != old.weak || found->particle_template != old.particle_template
            || found->tick_entry != old.tick_entry || found->attach_parent != old.attach_parent
            || found->attach_socket != old.attach_socket || found->lux != old.lux)
            return Status::failure(FailureCode::GenerationMismatch);
    }
    }
    std::uint64_t fingerprint{}, epoch{};
    if (!PreviousFingerprint(fingerprint) || fingerprint != previous_fingerprint_ || !ValidatePayload(false).ok()
        || !CopyBytes(&epoch, reinterpret_cast<void*>(observed.base_ + 0x4197170), sizeof(epoch)))
        return Status::failure(FailureCode::RestoreVerificationFailed);
    try {
        std::array<Array, 5> arrays{};
        std::array<std::byte, 0x50> map{};
        std::vector<Allocation> storage;
        std::vector<const std::byte*> providers;
        const auto slots = observed.constructed_;
        const auto records = allocations_.size() + 8;
        if (records > execution_reservation_ / sizeof(Allocation)
            || slots > (execution_reservation_ - records * sizeof(Allocation)) / sizeof(const std::byte*))
            return Status::failure(FailureCode::CapacityExceeded);
        storage.reserve(records); providers.reserve(slots);
        std::size_t used = storage.capacity() * sizeof(Allocation) + providers.capacity() * sizeof(const std::byte*);
        if (used > execution_reservation_) return Status::failure(FailureCode::CapacityExceeded);
        for (const auto& b : allocations_) storage.push_back(b);
        const auto quantify = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(observed.base_ + 0xd50dc0);
        const auto add = [&](void* pointer, std::size_t extent) -> bool {
            if (bool(pointer) != bool(extent) || (extent && extent > UINTPTR_MAX - reinterpret_cast<std::uintptr_t>(pointer))) return false;
            const auto charge = extent ? quantify(extent, 0) : 0;
            if (charge < extent || charge > execution_reservation_ - used) return false;
            if (extent) { storage.push_back({pointer, nullptr, extent, 0}); used += charge; }
            return true;
        };
        std::size_t provider_bytes{};
        for (std::size_t a = 0; a < arrays.size(); ++a) {
            if (!CopyBytes(&arrays[a], reinterpret_cast<void*>(observed.manager_
                + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2])), sizeof(Array))
                || arrays[a].count < 0 || arrays[a].capacity < arrays[a].count
                || arrays[a].capacity != (a < 2 ? observed.slot_capacities_[a] : observed.tables_[a - 2].native_capacity)
                || !add(arrays[a].data, static_cast<std::size_t>(arrays[a].capacity) * (a < 2 ? 0xc0 : table_strides[a - 2])))
                return Status::failure(FailureCode::RestorePreflightFailed);
            if (a < 2) for (int i = 0; i < arrays[a].count; ++i) {
                if (providers.size() == slots) return Status::failure(FailureCode::GenerationMismatch);
                const auto* request = static_cast<const std::byte*>(arrays[a].data) + i * 0xc0 + 0x10;
                status = ValidateProvider(observed.base_, request, provider_bytes);
                if (!status.ok()) return status;
                providers.push_back(Provider(request));
            }
        }
        if (providers.size() != slots || provider_bytes > execution_reservation_ - used)
            return Status::failure(FailureCode::CapacityExceeded);
        used += provider_bytes;
        if (!CopyBytes(map.data(), reinterpret_cast<void*>(observed.manager_ + 0x408), map.size()))
            return Status::failure(FailureCode::ContextUnavailable);
        constexpr std::array<std::size_t, 3> offsets{0, 0x20, 0x40};
        const std::array<std::size_t, 3> extents{
            static_cast<std::size_t>(At<int>(map.data(), 0xc)) * 32,
            ((static_cast<std::size_t>(At<int>(map.data(), 0x2c)) + 31) / 32) * 4,
            static_cast<std::size_t>(At<int>(map.data(), 0x48)) * 4};
        for (std::size_t i = 0; i < 3; ++i) {
            auto* pointer = At<void*>(map.data(), offsets[i]);
            if ((i == 0 || pointer) && !add(pointer, extents[i]))
                return Status::failure(FailureCode::RestorePreflightFailed);
        }
        const auto overlaps_root = [&](std::uintptr_t p, std::size_t n) {
            if (Overlaps(p, n, observed.manager_ + 0x3e0, 4) || Overlaps(p, n, observed.manager_ + 0x408, 0x50)) return true;
            for (std::size_t a = 0; a < arrays.size(); ++a)
                if (Overlaps(p, n, observed.manager_ + (a < 2 ? 0x3e8 + a * 16 : table_offsets[a - 2]), sizeof(Array))) return true;
            return false;
        };
        for (std::size_t i = 0; i < storage.size(); ++i) {
            const auto& c = storage[i]; if (!c.target_bytes) continue;
            const auto p = reinterpret_cast<std::uintptr_t>(c.target);
            if (overlaps_root(p, c.target_bytes)) return Status::failure(FailureCode::RestorePreflightFailed);
            for (std::size_t j = 0; j < storage.size(); ++j)
                if (Overlaps(p, c.target_bytes, reinterpret_cast<std::uintptr_t>(storage[j].previous), storage[j].previous_bytes)
                    || (j != i && Overlaps(p, c.target_bytes, reinterpret_cast<std::uintptr_t>(storage[j].target), storage[j].target_bytes)))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            for (const auto* b : provider_bindings_[0]) if (b && Overlaps(p, c.target_bytes, reinterpret_cast<std::uintptr_t>(b), 48))
                return Status::failure(FailureCode::RestorePreflightFailed);
        }
        for (std::size_t i = 0; i < providers.size(); ++i) {
            const auto p = reinterpret_cast<std::uintptr_t>(providers[i]); if (!p) continue;
            if (overlaps_root(p, 48)) return Status::failure(FailureCode::RestorePreflightFailed);
            for (const auto& a : storage)
                if (Overlaps(p, 48, reinterpret_cast<std::uintptr_t>(a.target), a.target_bytes)
                    || Overlaps(p, 48, reinterpret_cast<std::uintptr_t>(a.previous), a.previous_bytes))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            for (const auto* b : provider_bindings_[0]) if (b && Overlaps(p, 48, reinterpret_cast<std::uintptr_t>(b), 48))
                return Status::failure(FailureCode::RestorePreflightFailed);
            for (std::size_t j = 0; j < i; ++j) if (providers[j] && Overlaps(p, 48, reinterpret_cast<std::uintptr_t>(providers[j]), 48))
                return Status::failure(FailureCode::RestorePreflightFailed);
        }
        // All failure-prone ownership discovery precedes adoption. Until here
        // native code owns C and the transaction retains only private B.
        allocations_.swap(storage); provider_bindings_[1].swap(providers);
        arrays_ = arrays; map_ = map; target_ = &observed; epoch_ = epoch;
        execution_settlement_=purpose;
        execution_settled_ = true;
        return ValidatePublished();
    } catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

Status Sc6ReplayVfxState::PreparedManager::ReopenExecutionForUndo(const Sc6ReplayVfxState& target) noexcept
{
    if(!executing_ || !published_ || undo_started_ || !target_ || target.manager_!=target_->manager_
        || target.manager_weak_!=target_->manager_weak_)return Status::failure(FailureCode::IllegalTransition);
    if(!execution_settled_)return Status::success();
    auto status=ValidatePublished();if(status.ok())status=ValidatePayload(false);
    std::uint64_t fingerprint{};
    if(!status.ok())return status;
    if(!PreviousFingerprint(fingerprint) || fingerprint!=previous_fingerprint_)
        return Status::failure(FailureCode::RestoreVerificationFailed);
    for(auto& allocation:allocations_){allocation.target=nullptr;allocation.target_bytes=0;}
    std::erase_if(allocations_,[](const auto& allocation){return !allocation.previous;});
    arrays_={};map_={};provider_bindings_[1].clear();target_=&target;
    execution_settled_=false;execution_settlement_=RestoreSettlement::RecoverOriginal;
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedManager::Undo() noexcept
{
    if (!published_) return Status::success();
    if(executing_ && execution_settlement_!=RestoreSettlement::RecoverOriginal)
        return Status::failure(FailureCode::UndoFailed);
    if (executing_ && !execution_settled_) return Status::failure(FailureCode::UndoFailed);
    std::uint64_t fingerprint{};
    if (executing_ && (!PreviousFingerprint(fingerprint) || fingerprint != previous_fingerprint_))
        return Status::failure(FailureCode::UndoFailed);
    if (!ValidateBinding().ok() || (write_complete_ && !undo_started_ && !ValidatePublished().ok())
        || !ValidatePayload(false).ok()) return Status::failure(FailureCode::UndoFailed);
    undo_started_ = true;
    if (!Write(false) || !ValidateImage(false).ok()) return Status::failure(FailureCode::UndoFailed);
    published_ = write_complete_ = undo_started_ = false;
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedManager::Commit() noexcept
{
    auto status = ValidatePublished();
    if (status.ok()) status = ValidatePayload(false);
    std::uint64_t fingerprint{};
    if (status.ok() && executing_ && (!PreviousFingerprint(fingerprint) || fingerprint != previous_fingerprint_))
        status = Status::failure(FailureCode::RestoreVerificationFailed);
    if (!status.ok()) return status;
    published_ = false;
    Clear(true);
    return Status::success();
}

std::size_t Sc6ReplayVfxState::PreparedEmitterSet::owned_bytes() const noexcept
{
    auto bytes = sizeof(*this) + entries_.capacity() * sizeof(Entry) + reconstruction_bindings_.capacity() * sizeof(ReconstructionBinding);
    for (const auto& entry : entries_) if (entry.replacement) bytes += entry.replacement->owned_bytes();
    return bytes;
}

bool Sc6ReplayVfxState::PreparedEmitterSet::published() const noexcept
{
    for (const auto& entry : entries_) if (entry.replacement->published()) return true;
    return false;
}
Status Sc6ReplayVfxState::ParticleBirthSet::ValidateLifetimes() const noexcept
{
    for (const auto& member : members()) { const auto status = member.ValidateLifetime(); if (!status.ok()) return status; }
    return Status::success();
}

const Sc6ReplayCpuEmitterState* Sc6ReplayVfxState::PreparedEmitterSet::FindImage(
    const Sc6ReplayVfxState& image, const Entry& entry, bool original) const noexcept
{
    if (original && entry.reconstructed) return nullptr;
    if (gpu_storage_) {
        for (const auto& owner : image.gpu_owners_)
            if (owner.component == reinterpret_cast<std::uintptr_t>(entry.component)
                && owner.ordinal == entry.ordinal
                && (!original || owner.emitter == reinterpret_cast<std::uintptr_t>(entry.expected)))
                return &owner.image;
    } else {
        if (original && !entry.expected) return nullptr;
        for (const auto& emitter : image.cpu_emitters_)
            if (emitter.component == reinterpret_cast<std::uintptr_t>(entry.component)
                && emitter.ordinal == entry.ordinal) return &emitter.image;
    }
    return nullptr;
}

Status Sc6ReplayVfxState::PreparedEmitterSet::BeginExecution(
    const Sc6ReplayVfxState& original, std::size_t retirement_budget, const ReplayGpuCompletion* completion) noexcept
{
    if (execution_original_) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateCommit(original);
    if (!status.ok()) return status;
    // Allocate the admission envelope before handing off any address. Each
    // child charges its reservation, including scratch used on failed settle.
    std::size_t minimum{};
    for (const auto& entry : entries_) {
        const auto bytes = entry.replacement->owned_bytes();
        if (minimum > retirement_budget || bytes > retirement_budget - minimum)
            return Status::failure(FailureCode::CapacityExceeded);
        minimum += bytes;
    }
    if (retirement_budget > SIZE_MAX - owned_bytes())
        return Status::failure(FailureCode::CapacityExceeded);
    const auto extra = (retirement_budget - minimum) / entries_.size();
    execution_original_ = &original;
    for (auto& entry : entries_) {
        status = entry.replacement->BeginExecution(FindImage(original, entry, true),
            entry.replacement->owned_bytes() + extra, entry.reconstructed ? completion : nullptr);
        // Earlier children have handed A to native code. Keep the original
        // and their phase flags: even a failed admission requires settlement
        // before undo, rather than freeing old A addresses.
        if (!status.ok()) return status;
    }
    return Status::success();
}

bool Sc6ReplayVfxState::PreparedEmitterSet::FreshComponentDeath(std::uintptr_t component,
    const std::array<std::int32_t,2>& weak) const noexcept
{
    std::size_t found{};
    for (const auto& entry : entries_) if (entry.reconstructed && entry.identity.target == component) {
        if (entry.identity.target_weak != weak || !entry.replacement
            || !entry.replacement->FreshComponentDestructionWitness(component, weak)) return false;
        ++found;
    }
    return found != 0; // Every matching root above must carry its own death receipt.
}

bool Sc6ReplayVfxState::PreparedEmitterSet::FreshComponentRetired(std::uintptr_t component,
    const std::array<std::int32_t,2>& weak) const noexcept
{
    std::size_t found{};
    for (const auto& entry : entries_) if (entry.reconstructed && entry.identity.target == component) {
        if (entry.identity.target_weak != weak || !entry.replacement
            || !entry.replacement->FreshComponentRetirementWitness(component, weak)) return false;
        ++found;
    }
    return found != 0; // All matching roots must also have completed GPU retirement.
}

Status Sc6ReplayVfxState::PreparedEmitterSet::SettleExecution(const Sc6ReplayVfxState& observed, SettlementFailure* output_failure) noexcept
{
    if(output_failure){*output_failure={};output_failure->detail.phase="set_admission";}
    if (!execution_original_ || execution_undo_started_ || !observed.valid_
        || observed.thread_ != GetCurrentThreadId() || !observed.object_lease_
        || !execution_original_->valid_ || !execution_original_->object_lease_
        || observed.base_ != execution_original_->base_)
        return Status::failure(FailureCode::IllegalTransition);
    if(output_failure)output_failure->detail.phase="observed_lease";
    auto status = observed.object_lease_->Validate();
    if (status.ok()) {
        if(output_failure)output_failure->detail.phase="original_lease";
        status = execution_original_->object_lease_->Validate();
    }
    if (!status.ok()) return status;
    const auto root = [](const Sc6ReplayVfxState& image, std::uintptr_t component, std::size_t ordinal) -> const void* {
        for (const auto& owner : image.components_)
            if (owner.address == component && ordinal < owner.emitters.size())
                return reinterpret_cast<const void*>(owner.emitters[ordinal]);
        return nullptr;
    };
    const auto destroyed_member = [&](const auto& entry) {
        if (!entry.replacement->native_destroyed() || FindImage(observed,entry,false)) return false;
        if (entry.reconstructed) {
            for (const auto& owner : observed.components_)
                if (owner.address == entry.identity.target && owner.weak == entry.identity.target_weak)
                    return entry.ordinal<owner.emitters.size() && !owner.emitters[entry.ordinal]
                        && (!gpu_storage_ || entry.replacement->FreshGpuDestructionWitness());
            return FreshComponentDeath(entry.identity.target, entry.identity.target_weak);
        }
        if (gpu_storage_) return false;
        for(const auto& owner:observed.components_)
            if(owner.address==reinterpret_cast<std::uintptr_t>(entry.component))
                return entry.ordinal<owner.emitters.size() && owner.emitters[entry.ordinal]==0;
        return false;
    };
    // A child validates its own displaced B, but another emitter's backing
    // is equally protected. Check the whole captured C/B ownership partition
    // before adopting any C allocation for subsequent release.
    for (const auto& entry : entries_) {
        if (!entry.replacement->execution_started()) continue;
        const auto* image = FindImage(observed, entry, false);
        const auto address = reinterpret_cast<std::uintptr_t>(entry.component);
        const auto* live = root(observed, address, entry.ordinal);
        // Native deletion leaves no C graph to adopt or compare for overlap.
        // The captured owner/ordinal must still exist with a null slot; the
        // child separately verifies destructor completion, array identity and B.
        if(destroyed_member(entry))continue;
        if (!image || !live) {
            if(output_failure)*output_failure={{"observed_member",image?"missing_root":"missing_image",0,
                reinterpret_cast<std::uintptr_t>(entry.replacement->get()),reinterpret_cast<std::uintptr_t>(live)},entry.component,entry.ordinal};
            if(output_failure)output_failure->observed_member_flags=16u|(entry.replacement->native_destroyed()?1u:0u)
                |(entry.reconstructed?2u:0u)|(entry.reconstructed && FreshComponentDeath(entry.identity.target,entry.identity.target_weak)?4u:0u)
                |(observed.RetainsComponent(entry.component)?8u:0u)
                |(entry.replacement->destruction_diagnostic()<<8);
            return Status::failure(FailureCode::GenerationMismatch);
        }
        const auto check = [&](const Sc6ReplayVfxState& all, bool original) {
            for (const auto& cpu : all.cpu_emitters_) {
                if (!original && cpu.component == address && cpu.ordinal == entry.ordinal) continue;
                if (!image->StorageDisjoint(live, cpu.image, root(all, cpu.component, cpu.ordinal))) return false;
            }
            for (const auto& gpu : all.gpu_owners_) {
                const bool same = gpu.component == address && gpu.ordinal == entry.ordinal;
                if (!original && same) continue;
                if (!image->StorageDisjoint(live, gpu.image, reinterpret_cast<const void*>(gpu.emitter),
                    original && same && gpu_storage_)) return false;
            }
            return true;
        };
        if (!check(*execution_original_, true) || !check(observed, false))
            return Status::failure(FailureCode::RestorePreflightFailed);
    }
    for (auto& entry : entries_) {
        auto& replacement = *entry.replacement;
        if (!replacement.execution_started() || replacement.execution_settled()) continue;
        const auto* current = FindImage(observed, entry, false);
        Sc6ReplayCpuEmitterState::ValidationFailure failure{};
        if(destroyed_member(entry)) {
            failure.phase="native_destruction";
            status=replacement.SettleNativeDestruction(FindImage(*execution_original_,entry,true));
        } else {
            if (!current) return Status::failure(FailureCode::GenerationMismatch);
            status = replacement.SettleExecution(*current, FindImage(*execution_original_, entry, true), &failure);
        }
        if (!status.ok()) {
            if(output_failure)*output_failure={failure,entry.component,entry.ordinal};
            return status;
        }
    }
    // Includes children that never began after a partial admission, and
    // rechecks B's complete displaced graphs before any retirement or undo.
    status = ValidateCommit(*execution_original_,output_failure);
    if (status.ok()) execution_settled_ = true;
    return status;
}

Status Sc6ReplayVfxState::PreparedEmitterSet::ReopenExecutionForUndo() noexcept
{
    if(!execution_original_ || execution_undo_started_)return Status::failure(FailureCode::IllegalTransition);
    // Each child can already be settled after a partial set failure. Reopen
    // idempotently so a later rejected child never loses an earlier B owner.
    for(auto& entry:entries_) {
        if(!entry.replacement->execution_started())continue;
        auto status=entry.replacement->ReopenExecutionForUndo(FindImage(*execution_original_,entry,true));
        if(!status.ok())return status;
    }
    execution_settled_=false;return Status::success();
}

Status Sc6ReplayVfxState::PreparedEmitterSet::ValidatePublished(SettlementFailure* failure) const noexcept
{
    if (entries_.empty()) return Status::failure(FailureCode::IllegalTransition);
    for (const auto& entry : entries_)
    {
        if (!entry.replacement->published()) return Status::failure(FailureCode::IllegalTransition);
        const auto status = entry.replacement->ValidatePublished();
        if (!status.ok()) {
            if(failure)*failure={{"published_child","slot_or_fingerprint"},entry.component,entry.ordinal};
            return status;
        }
    }
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedEmitterSet::ValidateCommit(const Sc6ReplayVfxState& current, SettlementFailure* failure) const noexcept
{
    if(failure)failure->detail.phase="commit_original_lease";
    if (!current.valid_ || current.thread_ != GetCurrentThreadId() || !current.object_lease_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto status = current.object_lease_->Validate();
    if (status.ok() && reconstruction_source_) status = reconstruction_source_->ValidateReconstructionDependencies();
    if (status.ok()) status = ValidatePublished(failure);
    if (!status.ok()) return status;
    for (const auto& entry : entries_)
    {
        if (entry.reconstructed) {
            if (current.RetainsComponent(entry.component) || (!gpu_storage_ && entry.expected))
                return Status::failure(FailureCode::GenerationMismatch);
            for (const auto& owner : current.gpu_owners_)
                if (owner.emitter == reinterpret_cast<std::uintptr_t>(entry.expected))
                    return Status::failure(FailureCode::GenerationMismatch);
            continue;
        }
        const Sc6ReplayCpuEmitterState* image = nullptr;
        if (gpu_storage_)
        {
            for (const auto& owner : current.gpu_owners_)
                if (owner.component == reinterpret_cast<std::uintptr_t>(entry.component)
                    && owner.ordinal == entry.ordinal && owner.emitter == reinterpret_cast<std::uintptr_t>(entry.expected))
                { image = &owner.image; break; }
        }
        else if (entry.expected)
        {
            for (const auto& emitter : current.cpu_emitters_)
                if (emitter.component == reinterpret_cast<std::uintptr_t>(entry.component) && emitter.ordinal == entry.ordinal)
                { image = &emitter.image; break; }
        }
        else continue; // A retired CPU slot was null at B: no displaced object.
        if (!image) {
            if(failure)*failure={{"commit_original_member","missing_image"},entry.component,entry.ordinal};
            return Status::failure(FailureCode::GenerationMismatch);
        }
        status = image->ValidateDisplaced(*entry.replacement);
        if (!status.ok()) {
            if(failure)*failure={{"commit_original_displaced","validation"},entry.component,entry.ordinal};
            return status;
        }
    }
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedEmitterSet::Commit(const Sc6ReplayVfxState& current) noexcept
{
    const auto ready = ValidateCommit(current);
    if (!ready.ok()) return ready;
    for (auto& entry : entries_)
    {
        if (entry.reconstructed) {
            void* displaced{};
            const auto status = entry.replacement->CommitPublication(displaced);
            if (!status.ok() || displaced) return status.ok() ? Status::failure(FailureCode::GenerationMismatch) : status;
        }
        else if (gpu_storage_)
        {
            const Sc6ReplayCpuEmitterState* image = nullptr;
            for (const auto& owner : current.gpu_owners_)
                if (owner.component == reinterpret_cast<std::uintptr_t>(entry.component) && owner.ordinal == entry.ordinal)
                { image = &owner.image; break; }
            if (!image) return Status::failure(FailureCode::GenerationMismatch);
            const auto status = image->CommitDisplacedGpu(*entry.replacement);
            if (!status.ok()) return status;
        }
        else
        {
            void* displaced{};
            const auto status = entry.replacement->CommitPublication(displaced);
            if (!status.ok()) return status;
            // 141F908C0 -> 141F8F9E0. The retained B image admitted empty
            // attached-object/module storage, so this only retires its native
            // particle/index/burst/duration backing and emitter allocation.
            if (displaced) reinterpret_cast<void* (*)(void*, std::uint64_t)>(current.base_ + 0x1f908c0)(displaced, 1);
        }
    }
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedEmitterSet::Undo() noexcept
{
    if (execution_original_ && !execution_undo_started_) {
        if (!execution_settled_) return Status::failure(FailureCode::IllegalTransition);
        const auto status = ValidateCommit(*execution_original_);
        if (!status.ok()) return status;
        execution_undo_started_ = true;
    }
    for (auto i = entries_.size(); i > 0; --i)
    {
        auto& replacement = *entries_[i - 1].replacement;
        if (!replacement.published()) continue;
        if (!replacement.UndoPublication().ok()) return Status::failure(FailureCode::UndoFailed);
    }
    return Status::success();
}

Status Sc6ReplayVfxState::PreparedEmitterSet::Publish() noexcept
{
    if (gpu_storage_) return Status::failure(FailureCode::UnsupportedContent);
    if (entries_.empty() || published()) return Status::failure(FailureCode::IllegalTransition);
    if (reconstruction_source_) {
        const auto bindings=reconstruction_source_->ValidateReconstructionBindings(reconstruction_bindings_);
        if (!bindings.ok()) return bindings;
    }
    // Complete preflight before the first native slot write. Per-entry CAS
    // still detects invalidation between preflight and installation.
    for (auto& entry : entries_)
    {
        const auto status = entry.replacement->ValidateDestination(entry.ordinal, entry.expected);
        if (!status.ok()) return status;
    }
    for (auto& entry : entries_)
    {
        auto status = entry.replacement->Publish(entry.ordinal, entry.expected);
        if (!status.ok()) return Undo().ok() ? status : Status::failure(FailureCode::UndoFailed);
    }
    auto status = ValidatePublished();
    if (status.ok()) return status;
    return Undo().ok() ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayVfxState::PreparedEmitterSet::PublishGpuStorage() noexcept
{
    if (!gpu_storage_ || entries_.empty() || published()) return Status::failure(FailureCode::IllegalTransition);
    if (reconstruction_source_) {
        const auto bindings = reconstruction_source_->ValidateReconstructionBindings(reconstruction_bindings_);
        if (!bindings.ok()) return bindings;
    }
    for (auto& entry : entries_)
    {
        const auto status = entry.reconstructed ? entry.replacement->ValidateFreshGpuDestination(entry.ordinal, entry.expected)
            : entry.replacement->ValidateGpuDestination(entry.ordinal, entry.expected);
        if (!status.ok()) return status;
    }
    for (auto& entry : entries_)
    {
        const auto status = entry.reconstructed ? entry.replacement->PublishFreshGpuStorage(entry.ordinal, entry.expected, entry.fresh_write_started)
            : entry.replacement->PublishGpuStorage(entry.ordinal, entry.expected);
        if (!status.ok()) return Undo().ok() ? status : Status::failure(FailureCode::UndoFailed);
    }
    const auto status = ValidatePublished();
    if (status.ok()) return status;
    return Undo().ok() ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayVfxState::PrepareCpuEmitters(std::size_t budget, PreparedEmitterSet& output, std::span<const ReconstructionBinding> bindings) const noexcept
{
    return PrepareEmitters(budget, output, false, bindings);
}
Status Sc6ReplayVfxState::PrepareGpuEmitters(std::size_t budget, PreparedEmitterSet& output, std::span<const ReconstructionBinding> bindings) const noexcept
{
    return PrepareEmitters(budget, output, true, bindings);
}
Status Sc6ReplayVfxState::PrepareEmitters(std::size_t budget, PreparedEmitterSet& output, bool gpu, std::span<const ReconstructionBinding> bindings) const noexcept
{
    const auto count = gpu ? gpu_owners_.size() : cpu_emitters_.size();
    if (!output.entries_.empty() || !count) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || !object_lease_ || thread_ != GetCurrentThreadId()) return Status::failure(FailureCode::GenerationMismatch);
    auto retained = bindings.empty() ? object_lease_->Validate() : ValidateReconstructionBindings(bindings);
    if (!retained.ok()) return retained;
    const auto image_bytes = owned_bytes();
    if (image_bytes > budget || sizeof(PreparedEmitterSet) > budget - image_bytes
        || count > (budget - image_bytes - sizeof(PreparedEmitterSet)) / sizeof(PreparedEmitterSet::Entry))
        return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        PreparedEmitterSet next;
        next.gpu_storage_ = gpu;
        if (!bindings.empty()) {
            if (bindings.size() > (budget - image_bytes - sizeof(PreparedEmitterSet)) / sizeof(ReconstructionBinding))
                return Status::failure(FailureCode::CapacityExceeded);
            next.reconstruction_source_ = this;
            next.reconstruction_bindings_.assign(bindings.begin(), bindings.end());
        }
        next.entries_.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto used = next.owned_bytes();
            if (used > budget - image_bytes || sizeof(Sc6ReplayCpuEmitterState::Prepared) > budget - image_bytes - used)
                return Status::failure(FailureCode::CapacityExceeded);
            next.entries_.emplace_back();
            auto& entry = next.entries_.back();
            entry.replacement = std::make_unique<Sc6ReplayCpuEmitterState::Prepared>();
            const auto& image = gpu ? gpu_owners_[i].image : cpu_emitters_[i].image;
            entry.component = reinterpret_cast<void*>(gpu ? gpu_owners_[i].component : cpu_emitters_[i].component);
            entry.ordinal = gpu ? gpu_owners_[i].ordinal : cpu_emitters_[i].ordinal;
            // The image helper charges its own snapshot. Credit only that
            // already-counted image while reserving all other batch ownership.
            const ReconstructionBinding* mapping{};
            for (const auto& candidate : bindings)
                if (candidate.identity.source == reinterpret_cast<std::uintptr_t>(entry.component)) { mapping = &candidate; break; }
            if (!bindings.empty() && !mapping) return Status::failure(FailureCode::GenerationMismatch);
            entry.reconstructed = mapping && mapping->identity.source != mapping->identity.target;
            if (entry.reconstructed) {
                std::size_t live{}, gpu_count{};
                for (const auto& component : components_) if (component.address == mapping->identity.source)
                    for (auto root : component.emitters) live += root != 0;
                for (const auto& owner : gpu_owners_) gpu_count += owner.component == mapping->identity.source;
                std::size_t cpu_count{};
                for(const auto& owner:cpu_emitters_)cpu_count+=owner.component==mapping->identity.source;
                if (!live || gpu_count > 128 || live!=gpu_count+cpu_count) return Status::failure(FailureCode::UnsupportedContent);
                entry.identity = mapping->identity;
                entry.component = reinterpret_cast<void*>(entry.identity.target);
            }
            auto status = entry.reconstructed
                ? image.PrepareReplacement(budget - image_bytes - used + image.owned_bytes(), *entry.replacement, entry.identity)
                : image.PrepareReplacement(budget - image_bytes - used + image.owned_bytes(), *entry.replacement);
            if (!status.ok()) return status;
            Header current{};
            if (!CopyBytes(&current, static_cast<std::byte*>(entry.component) + 0xa50, sizeof(current))
                || current.count < 0 || current.count > 128 || current.capacity < current.count
                || !current.data || entry.ordinal >= static_cast<std::size_t>(current.count)
                || !CopyBytes(&entry.expected, current.data + entry.ordinal * sizeof(void*), sizeof(void*)))
                return Status::failure(FailureCode::GenerationMismatch);
            if (next.owned_bytes() > budget - image_bytes) return Status::failure(FailureCode::CapacityExceeded);
        }
        for (auto& entry : next.entries_)
        {
            const auto status = gpu
                ? (entry.reconstructed ? entry.replacement->ValidateFreshGpuDestination(entry.ordinal, entry.expected)
                    : entry.replacement->ValidateGpuDestination(entry.ordinal, entry.expected))
                : entry.replacement->ValidateDestination(entry.ordinal, entry.expected);
            if (!status.ok()) return status;
        }
        if (!bindings.empty()) {
            const auto validated = ValidateReconstructionBindings(bindings);
            if (!validated.ok()) return validated;
        }
        output.reconstruction_source_ = next.reconstruction_source_;
        output.reconstruction_bindings_.swap(next.reconstruction_bindings_);
        output.entries_.swap(next.entries_);
        output.gpu_storage_ = gpu;
        return Status::success();
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}

std::uint64_t Sc6ReplayVfxState::storage_fingerprint() const noexcept
{
    std::uint64_t value = 14695981039346656037ull;
    const auto add = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) { value ^= bytes[i]; value *= 1099511628211ull; }
    };
    add(&sequence_, sizeof(sequence_)); add(counts_.data(), sizeof(counts_));
    add(slot_capacities_.data(), sizeof(slot_capacities_));
    for (const auto& component : components_)
    {
        add(&component.address, sizeof(component.address)); add(component.weak.data(), sizeof(component.weak));
        add(component.values.data(), component.values.size());
        add(&component.active_flags, sizeof(component.active_flags));
        add(&component.tick_entry, sizeof(component.tick_entry));
        add(&component.attach_parent, sizeof(component.attach_parent));
        add(&component.attach_socket, sizeof(component.attach_socket));
        add(&component.particle_template, sizeof(component.particle_template));
        const auto count = component.emitters.size(); add(&count, sizeof(count));
        add(component.emitters.data(), count * sizeof(std::uintptr_t));
    }
    for (const auto& owner : gpu_owners_)
    {
        add(&owner.component, sizeof(owner.component)); add(&owner.ordinal, sizeof(owner.ordinal));
        add(&owner.emitter, sizeof(owner.emitter)); add(&owner.system, sizeof(owner.system));
        add(&owner.pool, sizeof(owner.pool)); add(&owner.render_storage, sizeof(owner.render_storage));
        add(&owner.coordinate_resource, sizeof(owner.coordinate_resource));
        const auto count = owner.tiles.size(); add(&count, sizeof(count));
        add(owner.tiles.data(), count * sizeof(std::uint32_t));
        const auto fingerprint = owner.image.storage_fingerprint(); add(&fingerprint, sizeof(fingerprint));
    }
    for (const auto& emitter : cpu_emitters_)
    {
        add(&emitter.component, sizeof(emitter.component)); add(&emitter.ordinal, sizeof(emitter.ordinal));
        const auto fingerprint = emitter.image.storage_fingerprint(); add(&fingerprint, sizeof(fingerprint));
    }
    for (const auto& pool : tile_pools_)
    {
        add(&pool.system, sizeof(pool.system)); add(&pool.address, sizeof(pool.address)); add(&pool.count, sizeof(pool.count));
        add(pool.free_indices.get(), pool.count * 4);
    }
    add(definition_layout_.data(), definition_layout_.size());
    for (std::size_t i = 0; i < definition_storage_.size(); ++i)
    {
        add(&definition_storage_[i].count, sizeof(definition_storage_[i].count));
        add(definition_storage_[i].bytes.get(), definition_storage_[i].count * definition_strides[i]);
    }
    for (std::size_t i = 0; i < tables_.size(); ++i)
    {
        add(&tables_[i].count, sizeof(tables_[i].count));
        add(&tables_[i].native_capacity, sizeof(tables_[i].native_capacity));
        add(tables_[i].bytes.get(), tables_[i].count * table_strides[i]);
    }
    for (std::size_t i = 0; i < constructed_; ++i)
    {
        const auto* p = slots_[i].bytes.data(); add(p, 0xc0);
        if (At<int>(p, 0xb0)) add(Provider(p + 0x10), 40);
    }
    return value; // Owned-storage retention witness, not a cross-process hash.
}
}
