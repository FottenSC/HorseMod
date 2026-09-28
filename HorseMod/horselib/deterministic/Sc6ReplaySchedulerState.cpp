#include "Sc6ReplaySchedulerState.hpp"
#include "ReplayComponentTickConsumer.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
bool ReadBytes(std::uintptr_t address, void* output, std::size_t bytes) noexcept
{
    if (!bytes) return true;
    if (!address || bytes > UINTPTR_MAX - address) return false;
    __try { std::memcpy(output, reinterpret_cast<const void*>(address), bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> bool Read(std::uintptr_t address, T& value) noexcept
{ return ReadBytes(address, &value, sizeof(value)); }
template<class T, std::size_t N> T Field(const std::array<std::byte, N>& bytes, std::size_t offset)
{ T value; std::memcpy(&value, bytes.data() + offset, sizeof(value)); return value; }
template<class T> bool ResizeCopy(std::vector<T>& output, std::uintptr_t address, std::size_t count, std::size_t remaining)
{
    if (count > remaining / sizeof(T)) return false;
    output.resize(count);
    return output.capacity() <= remaining / sizeof(T) && ReadBytes(address, output.data(), count * sizeof(T));
}
constexpr auto unavailable = FailureCode::ContextUnavailable;
constexpr auto capacity = FailureCode::CapacityExceeded;
constexpr auto invalid = FailureCode::RestorePreflightFailed;

bool AdmitComponentTickConsumer(std::uintptr_t base, std::uintptr_t tick_type, std::uintptr_t owner) noexcept
{
    return Horse::Deterministic::ReplayComponentTickConsumerAdmitted(base,tick_type,owner);
}

// Native map lookup 1415827A0 / accessor 142041C70 use 0x20-byte sparse
// entries: UWorld key, scene, reference controller, hash links. Scan occupied
// entries with a bound instead of following possibly invalid hash chains.
bool FindScene(std::uintptr_t base, std::uintptr_t world, std::uintptr_t& scene, std::uintptr_t& control) noexcept
{
    const auto map = base + 0x4096120;
    std::uintptr_t data{}, flags{};
    int slots{}, bits{};
    if (!Read(map, data) || !Read(map + 8, slots) || slots < 0 || slots > 65536
        || !Read(map + 0x28, bits) || bits != slots || !Read(map + 0x20, flags)
        || (!flags && bits > 128) || (slots && !data)) return false;
    if (!flags) flags = map + 0x10;
    bool found = false;
    for (int i = 0; i < slots; ++i)
    {
        std::uint32_t word{};
        if (!Read(flags + (i / 32) * 4, word)) return false;
        if (!(word & (1u << (i % 32)))) continue;
        std::uintptr_t key{};
        const auto entry = data + static_cast<std::size_t>(i) * 0x20;
        if (!Read(entry, key)) return false;
        if (key != world) continue;
        if (found || !Read(entry + 8, scene) || !Read(entry + 0x10, control)) return false;
        found = true;
    }
    return found && scene && control;
}
}

Sc6ReplaySchedulerState::SceneBinding::~SceneBinding() { Release(); }
Sc6ReplaySchedulerState::SceneBinding::SceneBinding(SceneBinding&& other) noexcept
    : scene(std::exchange(other.scene, 0)), control(std::exchange(other.control, 0)) {}
Sc6ReplaySchedulerState::SceneBinding& Sc6ReplaySchedulerState::SceneBinding::operator=(SceneBinding&& other) noexcept
{
    if (this != &other) { Release(); scene = std::exchange(other.scene, 0); control = std::exchange(other.control, 0); }
    return *this;
}
void Sc6ReplaySchedulerState::SceneBinding::Release() noexcept
{
    // These native non-thread-safe references are owned exclusively by the
    // replay owner thread. A weak lease retains only the controller, never the
    // scene or its world. Native removal 14113CF60 releases the implicit weak
    // reference after destroying the scene when its strong count reaches zero.
    if (!control) return;
    auto* weak = reinterpret_cast<int*>(control + 0xc);
    if (--*weak == 0)
    {
        const auto vtable = *reinterpret_cast<const std::uintptr_t*>(control);
        reinterpret_cast<void (*)(void*, unsigned)>(*reinterpret_cast<const std::uintptr_t*>(vtable + 8))(
            reinterpret_cast<void*>(control), 1u);
    }
    scene = control = 0;
}

Status Sc6ReplaySchedulerState::BindScene(std::uintptr_t scene) noexcept
{
    std::uintptr_t live{}, control{}, vtable{}, object{};
    int strong{}, weak{};
    if (!FindScene(base_, world_.object, live, control) || live != scene
        || !Read(control, vtable) || vtable != base_ + 0x35e56a0
        || !Read(control + 0x10, object) || object != scene
        || !Read(control + 8, strong) || strong <= 0
        || !Read(control + 0xc, weak) || weak <= 0 || weak == INT_MAX)
        return Status::failure(FailureCode::GenerationMismatch);
    if (scene_.control)
        return scene_.control == control && scene_.scene == scene ? Status::success()
            : Status::failure(FailureCode::GenerationMismatch);
    __try { ++*reinterpret_cast<int*>(control + 0xc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(unavailable); }
    scene_.scene = scene;
    scene_.control = control;
    return Status::success();
}

bool Sc6ReplaySchedulerState::IsLiveObject(const ObjectBinding& binding) const noexcept
{
    __try { return binding.object && reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data())
        == reinterpret_cast<void*>(binding.object); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplaySchedulerState::ValidateBindings(std::uintptr_t base, void* world,BindingFailure* failure) const noexcept
{
    if(failure){*failure={};failure->check="world";}
    if (!base_ || base != base_ || thread_ != GetCurrentThreadId() || levels_.empty() || ticks_.empty()
        || world_.object != reinterpret_cast<std::uintptr_t>(world) || !IsLiveObject(world_))
        return Status::failure(FailureCode::GenerationMismatch);
    if(failure)failure->check="scene";
    if (scene_.control)
    {
        std::uintptr_t scene{}, control{};
        int strong{};
        if (!FindScene(base_, world_.object, scene, control) || control != scene_.control || scene != scene_.scene
            || !Read(control + 8, strong) || strong <= 0) return Status::failure(FailureCode::GenerationMismatch);
    }
    if(failure)failure->check="level";
    for (const auto& level : levels_)
    {
        std::uintptr_t address{};
        std::uint8_t active{};
        int pending{};
        if (!IsLiveObject(level.owner)
            || !Read(level.owner.object + (level.owner.object == world_.object ? 0x778 : 0x140), address)
            || address != level.address) return Status::failure(FailureCode::GenerationMismatch);
        if (!Read(address + 0x128, active) || !Read(address + 0xb8, pending) || active || pending)
            return Status::failure(invalid);
    }
    for (const auto& tick : ticks_)
    {
        if(failure){*failure={};failure->check="tick_owner";failure->tick=tick.address;failure->owner=tick.owner.object;failure->weak=tick.owner.weak;}
        if (!IsLiveObject(tick.owner)) return Status::failure(FailureCode::GenerationMismatch);
        if(failure)failure->check="tick_consumer";
        if (!AdmitComponentTickConsumer(base_,Field<std::uintptr_t>(tick.binding_layout,0),tick.owner.object))
            return Status::failure(FailureCode::UnsupportedContent);
        std::array<std::byte, 0x58> live{};
        if(failure)failure->check="tick_read";
        if (!Read(tick.address, live)) return Status::failure(unavailable);
        for (const auto offset : {0u, 0x48u, 0x50u}) {
            if(failure){failure->check="tick_prefix";failure->offset=offset;failure->expected=Field<std::uintptr_t>(tick.binding_layout,offset);failure->actual=Field<std::uintptr_t>(live,offset);}
            if (Field<std::uintptr_t>(live, offset) != Field<std::uintptr_t>(tick.binding_layout, offset))
                return Status::failure(FailureCode::GenerationMismatch);
        }
        if(failure)failure->check="tick_pending_task";
        if (Field<std::uintptr_t>(live, 0x18)) return Status::failure(invalid);
    }
    if(failure)*failure={};
    return Status::success();
}

Status Sc6ReplaySchedulerState::ReadPrerequisiteConsumer(std::uintptr_t component,
    std::span<const ReplayPrerequisiteConsumer::Identity> sources,
    ReplayPrerequisiteConsumer::Contract& output) const noexcept
{
    using Consumer=ReplayPrerequisiteConsumer;
    output={};
    if(!base_ || thread_!=GetCurrentThreadId() || !IsLiveObject(world_))return Status::failure(unavailable);
    const auto found=std::find_if(ticks_.begin(),ticks_.end(),[&](const Tick& tick) {
        return tick.address==component+0x110 && tick.owner.object==component;
    });
    if(found==ticks_.end() || !IsLiveObject(found->owner)
        || found->prerequisites.size()>output.edges.size())return Status::failure(invalid);
    Consumer::Contract next{};
    next.owner={component,found->owner.weak[0],found->owner.weak[1]};
    next.world={world_.object,world_.weak[0],world_.weak[1]};
    next.tick=found->address;next.level=Field<std::uintptr_t>(found->binding_layout,0x48);
    next.tick_table=Field<std::uintptr_t>(found->binding_layout,0);
    if(next.tick_table!=base_+0x3865f98 || Field<std::uintptr_t>(found->binding_layout,0x50)!=component
        || !Read(component,next.owner_table) || next.owner_table!=base_+0x38829c0
        || !Read(next.owner_table+0x300,next.consumer) || next.consumer!=base_+0x1dafc60)
        return Status::failure(FailureCode::UnsupportedContent);
    next.count=static_cast<std::uint32_t>(found->prerequisites.size());
    for(std::size_t i=0;i<next.count;++i)
        std::memcpy(&next.edges[i],found->prerequisites[i].data(),sizeof(Consumer::Edge));
    if(next.count==1)for(const auto& source:sources)
        if(source.valid() && next.edges[0]==Consumer::Edge{source.index,source.serial,source.object+0x110}) {
            if(next.source.valid())return Status::failure(invalid);
            next.source=source;
        }
    if(!next.valid())return Status::failure(invalid);
    output=next;return Status::success();
}

std::size_t Sc6ReplaySchedulerState::owned_bytes() const noexcept
{
    // Count the controller conservatively: our weak lease may become its
    // only remaining owner after native scene removal.
    auto bytes = levels_.capacity() * sizeof(Level) + ticks_.capacity() * sizeof(Tick)
        + (scene_.control ? 0x18u : 0u);
    for (const auto& level : levels_)
    {
        bytes += level.cooldown_order.capacity() * sizeof(std::uintptr_t);
        for (const auto& set : level.sets)
            bytes += set.slots.capacity() + 4 * (set.allocation_flags.capacity() + set.hash.capacity());
    }
    for (const auto& tick : ticks_) bytes += tick.prerequisites.capacity() * 16;
    return bytes;
}

Status Sc6ReplaySchedulerState::RehashTickSetStorage(std::span<std::byte> slots,
    std::span<const std::uint32_t> occupied,std::span<std::int32_t> hashes) noexcept
{
    if(slots.size()%16 || slots.size()/16>INT_MAX || occupied.size()!=(slots.size()/16+31)/32
        || hashes.size()>INT_MAX || (hashes.size() && (hashes.size()&(hashes.size()-1))))return Status::failure(invalid);
    const auto count=slots.size()/16;
    const auto live=[&](std::size_t i){return (occupied[i/32]>>(i%32))&1u;};
    const auto key=[&](std::size_t i){std::uintptr_t value{};std::memcpy(&value,slots.data()+i*16,8);return value;};
    for(std::size_t i=0;i<count;++i)if(live(i)) {
        if(!hashes.size() || !key(i))return Status::failure(invalid);
        for(std::size_t j=0;j<i;++j)if(live(j) && key(j)==key(i))return Status::failure(invalid);
    }
    // Native14290B8E0 insertion,140E725D0 lookup and1421C2030 removal
    // use this same wrapping uint32 pointer hash. Sparse iteration order and
    // free-list slots remain intact; only occupied hash links are rebuilt.
    const auto hash=[](std::uintptr_t pointer) {
        const auto value=static_cast<std::uint32_t>(pointer>>4);
        auto a=(0x9e3779b9u-value)^(value<<8);
        auto b=(0u-a-value)^(a>>13);
        auto c=(value-a-b)^(b>>12);
        a=(a-c-b)^(c<<16);b=(b-a-c)^(a>>5);c=(c-a-b)^(b>>3);
        a=(a-c-b)^(c<<10);return (b-a-c)^(a>>15);
    };
    std::fill(hashes.begin(),hashes.end(),-1);
    for(std::size_t i=0;i<count;++i)if(live(i)) {
        const auto bucket=hash(key(i))&static_cast<std::uint32_t>(hashes.size()-1);
        std::memcpy(slots.data()+i*16+8,&hashes[bucket],4);
        std::memcpy(slots.data()+i*16+12,&bucket,4);hashes[bucket]=static_cast<std::int32_t>(i);
    }
    return Status::success();
}

bool Sc6ReplaySchedulerState::SameLogicalImage(const Sc6ReplaySchedulerState& other) const noexcept
{
    if (base_ != other.base_ || epoch_ != other.epoch_ || thread_ != other.thread_
        || world_.object != other.world_.object || world_.weak != other.world_.weak
        || scene_.scene != other.scene_.scene || scene_.control != other.scene_.control
        || levels_.size() != other.levels_.size() || ticks_.size() != other.ticks_.size()) return false;
    const auto equivalent_header = [](auto left, auto right, auto offsets) {
        for (const auto offset : offsets) {
            if (bool(Field<std::uintptr_t>(left, offset)) != bool(Field<std::uintptr_t>(right, offset))) return false;
            const std::uintptr_t zero{};
            std::memcpy(left.data() + offset, &zero, 8); std::memcpy(right.data() + offset, &zero, 8);
        }
        return left == right;
    };
    for (std::size_t i = 0; i < levels_.size(); ++i) {
        const auto& a = levels_[i]; const auto& b = other.levels_[i];
        if (a.address != b.address || a.owner.object != b.owner.object || a.owner.weak != b.owner.weak
            || a.cooldown_order != b.cooldown_order) return false;
        for (std::size_t j = 0; j < a.sets.size(); ++j) {
            const auto& x = a.sets[j]; const auto& y = b.sets[j];
            if (!equivalent_header(x.binding_layout, y.binding_layout, std::array<std::size_t,3>{0,0x20,0x40})
                || x.slots != y.slots || x.allocation_flags != y.allocation_flags || x.hash != y.hash) return false;
        }
    }
    for (const auto& a : ticks_) {
        const auto found = std::find_if(other.ticks_.begin(), other.ticks_.end(), [&](const auto& b) {return b.address == a.address;});
        if (found == other.ticks_.end() || a.owner.object != found->owner.object || a.owner.weak != found->owner.weak
            || !equivalent_header(a.binding_layout, found->binding_layout, std::array<std::size_t,1>{0x20})
            || a.prerequisites != found->prerequisites) return false;
    }
    return true;
}

Status Sc6ReplaySchedulerState::ReconstructComponentBindings(const Sc6ReplayVfxState& owners,
    std::span<const Sc6ReplayVfxState::ReconstructionBinding> bindings,
    std::size_t budget,Sc6ReplaySchedulerState& output,BindingFailure* failure) const noexcept
{
    return ReconstructOwnedComponentBindings({&owners,[](const void* context,std::span<const ComponentBinding> rows) {
        return static_cast<const Sc6ReplayVfxState*>(context)->ValidateReconstructionBindings(rows);
    }},bindings,budget,output,failure);
}
Status Sc6ReplaySchedulerState::ReconstructOwnedComponentBindings(ComponentOwnerProof owners,
    std::span<const ComponentBinding> bindings,std::size_t budget,
    Sc6ReplaySchedulerState& output,BindingFailure* failure) const noexcept
{
    if(failure){*failure={};failure->check="projection";}
    if(&output==this || output.base_ || !base_ || thread_!=GetCurrentThreadId())return Status::failure(invalid);
    if(!owners.context || !owners.validate || bindings.size()>128)return Status::failure(invalid);
    try {
    auto status=owners.validate(owners.context,bindings);if(!status.ok())return status;
    for(std::size_t i=0;i<bindings.size();++i) {
        const auto& id=bindings[i].identity;
        if(!id.source || !id.target || id.source_weak[0]<0 || id.target_weak[0]<0
            || id.source_weak[1]<=0 || id.target_weak[1]<=0
            || (id.source!=id.target && id.source_weak==id.target_weak))return Status::failure(invalid);
        for(std::size_t j=0;j<i;++j)if(bindings[j].identity.source==id.source || bindings[j].identity.target==id.target)
            return Status::failure(invalid);
    }
    if(owned_bytes()>budget)return Status::failure(capacity);
        Sc6ReplaySchedulerState next;
        next.base_=base_;next.epoch_=epoch_;next.thread_=thread_;next.world_=world_;
        if(scene_.control) {status=next.BindScene(scene_.scene);if(!status.ok())return status;}
        next.levels_=levels_;next.ticks_=ticks_;
        if(next.owned_bytes()>budget)return Status::failure(capacity);
        for(std::size_t i=0;i<ticks_.size();++i) {
            const auto& original=ticks_[i];auto& tick=next.ticks_[i];
            for(const auto& binding:bindings) {
                const auto& id=binding.identity;
                if(original.owner.object!=id.source || (id.source==id.target && id.source_weak==id.target_weak))continue;
                if(original.owner.weak!=id.source_weak || original.address<id.source)return Status::failure(invalid);
                const auto offset=original.address-id.source;
                if((offset!=0x110 && offset!=0x7b0) || id.target>UINTPTR_MAX-offset)return Status::failure(invalid);
                tick.owner={id.target,id.target_weak};tick.address=id.target+offset;
                std::memcpy(tick.binding_layout.data()+0x50,&id.target,8);
            }
        }
        const auto translate=[&](std::uintptr_t address) {
            for(std::size_t i=0;i<ticks_.size();++i)if(ticks_[i].address==address)return next.ticks_[i].address;
            return address;
        };
        for(auto& tick:next.ticks_) {
            auto successor=translate(Field<std::uintptr_t>(tick.binding_layout,0x30));
            std::memcpy(tick.binding_layout.data()+0x30,&successor,8);
            for(auto& prerequisite:tick.prerequisites) {
                auto address=Field<std::uintptr_t>(prerequisite,8);const auto replacement=translate(address);
                for(const auto& binding:bindings) {
                    const auto& id=binding.identity;
                    if(!std::memcmp(prerequisite.data(),id.source_weak.data(),8))
                        std::memcpy(prerequisite.data(),id.target_weak.data(),8);
                }
                std::memcpy(prerequisite.data()+8,&replacement,8);
            }
        }
        for(auto& level:next.levels_) {
            for(auto& tick:level.cooldown_order)tick=translate(tick);
            for(auto& set:level.sets) {
                if(set.slots.size()%16 || set.allocation_flags.size()!=(set.slots.size()/16+31)/32)return Status::failure(invalid);
                for(std::size_t i=0;i<set.slots.size()/16;++i)if((set.allocation_flags[i/32]>>(i%32))&1u) {
                    std::uintptr_t address{};std::memcpy(&address,set.slots.data()+i*16,8);address=translate(address);
                    std::memcpy(set.slots.data()+i*16,&address,8);
                }
                status=RehashTickSetStorage(set.slots,set.allocation_flags,set.hash);if(!status.ok())return status;
                if(!Field<std::uintptr_t>(set.binding_layout,0x40)) {
                    if(set.hash.size()>2)return Status::failure(invalid);
                    if(!set.hash.empty())std::memcpy(set.binding_layout.data()+0x38,set.hash.data(),set.hash.size()*4);
                }
            }
        }
        status=next.ValidateBindings(base_,reinterpret_cast<void*>(world_.object),failure);
        if(status.ok())status=owners.validate(owners.context,bindings);
        if(status.ok())output=std::move(next);
        return status;
    } catch(...) {return Status::failure(capacity);}
}

bool Sc6ReplaySchedulerState::OwnsStageComponent(const ReplayStageVisibility::Component& c) const noexcept
{
    if(!base_ || thread_!=GetCurrentThreadId() || !c.object || !(c.flags&1)
        || (!c.base_registration && !c.primitive_registration))return false;
    std::uintptr_t table{},entry{};
    if(!Read(c.object,table) || table!=c.type || !Read(table+0x2d8,entry)
        || entry!=c.registration_entry || entry!=base_+(c.primitive_registration?0x1da9000:0x1d58ac0))return false;
    const auto owns=[&](std::size_t offset,std::uint8_t flags) {
        std::uint8_t live{};
        if(!Read(c.object+offset+0xc,live) || ((live^flags)&~0x40u))return false;
        const auto found=std::find_if(ticks_.begin(),ticks_.end(),[&](const Tick& tick){return tick.address==c.object+offset;});
        if(found==ticks_.end())return !(flags&0x42) && !(live&0x42);
        return found->owner.object==c.object && found->owner.weak==c.weak && IsLiveObject(found->owner)
            && !((Field<std::uint8_t>(found->binding_layout,0xc)^flags)&~0x40u);
    };
    return owns(0x110,c.primary_tick_flags) && (!c.primitive_registration || owns(0x7b0,c.secondary_tick_flags));
}

std::size_t Sc6ReplaySchedulerState::prerequisite_count() const noexcept
{
    std::size_t count = 0;
    for (const auto& tick : ticks_) count += tick.prerequisites.size();
    return count;
}

Status Sc6ReplaySchedulerState::BindObject(std::uintptr_t object, ObjectBinding& binding) const noexcept
{
    if (!object) return Status::failure(unavailable);
    __try
    {
        // Native weak construction may allocate the object's serial, not a
        // simulation identity. These bindings stay outside canonical state.
        reinterpret_cast<void (*)(void*, const void*)>(base_ + 0xf7bad0)(binding.weak.data(), reinterpret_cast<const void*>(object));
        if (reinterpret_cast<void* (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data()) != reinterpret_cast<void*>(object))
            return Status::failure(FailureCode::GenerationMismatch);
        binding.object = object;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(unavailable); }
}

Status Sc6ReplaySchedulerState::CaptureSet(std::uintptr_t address, SetImage& output, std::size_t budget)
{
    if (!Read(address, output.binding_layout)) return Status::failure(unavailable);
    const auto& h = output.binding_layout;
    const auto slots = Field<int>(h, 8), slot_capacity = Field<int>(h, 0xc);
    const auto bits = Field<int>(h, 0x28), max_bits = Field<int>(h, 0x2c);
    const auto free = Field<int>(h, 0x34), hash_size = Field<int>(h, 0x48);
    const auto flags_address = Field<std::uintptr_t>(h, 0x20);
    if (slots < 0 || slot_capacity < slots || bits != slots || max_bits < bits || free < 0 || free > slots
        || hash_size < 0 || (hash_size && (hash_size & (hash_size - 1))) || (!flags_address && max_bits > 128))
        return Status::failure(invalid);
    const auto room = [&]() { const auto used = owned_bytes(); return used < budget ? budget - used : 0; };
    if (!ResizeCopy(output.slots, Field<std::uintptr_t>(h, 0), static_cast<std::size_t>(slots) * 16, room())
        || !ResizeCopy(output.allocation_flags, flags_address ? flags_address : address + 0x10,
            (static_cast<std::size_t>(bits) + 31) / 32, room())) return Status::failure(capacity);
    const auto hash_address = Field<std::uintptr_t>(h, 0x40);
    if (!hash_address && hash_size > 2) return Status::failure(invalid);
    if (!ResizeCopy(output.hash, hash_address ? hash_address : address + 0x38, hash_size, room()))
        return Status::failure(capacity);
    int occupied = 0;
    for (int i = 0; i < slots; ++i) occupied += (output.allocation_flags[i / 32] >> (i % 32)) & 1;
    return occupied == slots - free ? Status::success() : Status::failure(invalid);
}

Status Sc6ReplaySchedulerState::CaptureTick(std::uintptr_t address, std::uintptr_t level, std::size_t budget)
{
    if (auto found = std::find_if(ticks_.begin(), ticks_.end(), [address](const Tick& tick) { return tick.address == address; }); found != ticks_.end())
    {
        if (Field<std::uintptr_t>(found->binding_layout, 0x48) != level || !IsLiveObject(found->owner))
            return Status::failure(invalid);
        return AdmitComponentTickConsumer(base_,Field<std::uintptr_t>(found->binding_layout,0),found->owner.object)
            ? Status::success() : Status::failure(FailureCode::UnsupportedContent);
    }
    if (!address || owned_bytes() > budget || sizeof(Tick) > budget - owned_bytes()) return Status::failure(capacity);
    ticks_.emplace_back();
    auto& tick = ticks_.back();
    tick.address = address;
    if (owned_bytes() > budget) return Status::failure(capacity);
    if (!Read(address, tick.binding_layout)) return Status::failure(unavailable);
    const auto& h = tick.binding_layout;
    const auto vtable = Field<std::uintptr_t>(h, 0);
    constexpr std::array<std::uintptr_t, 6> kinds{0x381c720, 0x3865f98, 0x3982058, 0x3982070, 0x39efa78, 0x39efa90};
    if (vtable < base_ || std::find(kinds.begin(), kinds.end(), vtable - base_) == kinds.end())
        return Status::failure(FailureCode::UnsupportedContent);
    if (Field<std::uintptr_t>(h, 0x18) || Field<std::uintptr_t>(h, 0x48) != level)
        return Status::failure(invalid);
    auto owner = Field<std::uintptr_t>(h, 0x50);
    if (vtable - base_ == 0x3982058 || vtable - base_ == 0x3982070)
    {
        const auto scene_status = BindScene(owner);
        if (!scene_status.ok()) return scene_status;
        if (owned_bytes() > budget) return Status::failure(capacity);
        std::uintptr_t scene_vtable{};
        if (!Read(owner, scene_vtable) || scene_vtable != base_ + 0x3982088
            || !Read(owner + 0x10, owner) || owner != world_.object) return Status::failure(invalid);
    }
    auto status = BindObject(owner, tick.owner);
    if (!status.ok()) return status;
    if (!AdmitComponentTickConsumer(base_,vtable,owner)) return Status::failure(FailureCode::UnsupportedContent);
    const auto count = Field<int>(h, 0x28), max = Field<int>(h, 0x2c);
    if (count < 0 || max < count) return Status::failure(invalid);
    if (!ResizeCopy(tick.prerequisites, Field<std::uintptr_t>(h, 0x20), count, budget - owned_bytes()))
        return Status::failure(capacity);
    return Status::success();
}

Status Sc6ReplaySchedulerState::CaptureUnchecked(std::uintptr_t base, void* world, std::size_t budget)
{
    base_ = base;
    thread_ = GetCurrentThreadId();
    const auto address = reinterpret_cast<std::uintptr_t>(world);
    auto status = BindObject(address, world_);
    if (!status.ok()) return status;
    if (!Read(base + 0x4197170, epoch_)) return Status::failure(unavailable);
    std::uintptr_t loaded{}, root{};
    int count{};
    if (!Read(address + 0x110, loaded) || !Read(address + 0x118, count) || count < 0
        || !Read(address + 0x778, root) || !root) return Status::failure(unavailable);
    if (static_cast<std::size_t>(count) + 1 > budget / sizeof(Level)) return Status::failure(capacity);
    levels_.reserve(static_cast<std::size_t>(count) + 1);
    for (int i = -1; i < count; ++i)
    {
        std::uintptr_t owner = address, tick_level = root;
        if (i >= 0 && (!Read(loaded + i * 8, owner) || !Read(owner + 0x140, tick_level)))
            return Status::failure(unavailable);
        if (!tick_level || std::any_of(levels_.begin(), levels_.end(), [tick_level](const Level& level) { return level.address == tick_level; })) continue;
        levels_.emplace_back();
        auto& level = levels_.back();
        level.address = tick_level;
        status = BindObject(owner, level.owner);
        if (!status.ok()) return status;
        std::uint8_t active{};
        int pending{};
        if (!Read(tick_level + 0x128, active) || !Read(tick_level + 0xb8, pending) || active || pending)
            return Status::failure(invalid);
        constexpr std::array<std::size_t, 3> offsets{8, 0x60, 0xc0};
        for (std::size_t n = 0; n < offsets.size(); ++n)
        {
            status = CaptureSet(tick_level + offsets[n], level.sets[n], budget);
            if (!status.ok()) return status;
            const auto& set = level.sets[n];
            for (std::size_t slot = 0; slot < set.slots.size() / 16; ++slot)
            {
                if (!(set.allocation_flags[slot / 32] & (1u << (slot % 32)))) continue;
                std::uintptr_t tick{};
                std::memcpy(&tick, set.slots.data() + slot * 16, sizeof(tick));
                status = CaptureTick(tick, tick_level, budget);
                if (!status.ok()) return status;
            }
        }
        std::uintptr_t tick{};
        if (!Read(tick_level + 0x58, tick)) return Status::failure(unavailable);
        while (tick)
        {
            if (std::find(level.cooldown_order.begin(), level.cooldown_order.end(), tick) != level.cooldown_order.end())
                return Status::failure(invalid);
            if (owned_bytes() > budget || sizeof(tick) > budget - owned_bytes()) return Status::failure(capacity);
            level.cooldown_order.push_back(tick);
            status = CaptureTick(tick, tick_level, budget);
            if (!status.ok()) return status;
            if (!Read(tick + 0x30, tick)) return Status::failure(unavailable);
        }
    }
    std::uint64_t epoch{};
    if (!Read(base + 0x4197170, epoch) || epoch != epoch_ || owned_bytes() > budget)
        return Status::failure(invalid);
    return Status::success();
}

Status Sc6ReplaySchedulerState::Capture(std::uintptr_t base, void* world, std::size_t budget,
    RetainedPrimaryTicks retained, const Sc6ReplaySchedulerState* continuation_owner) noexcept
{
    if (budget <= owned_bytes()) return Status::failure(capacity);
    if(continuation_owner==this) return Status::failure(FailureCode::IllegalTransition);
    if(continuation_owner) {
        const auto status=continuation_owner->ValidateBindings(base,world);
        if(!status.ok())return status;
    }
    try
    {
        Sc6ReplaySchedulerState next;
        auto status = next.CaptureUnchecked(base, world, budget - owned_bytes());
        if(status.ok() && continuation_owner) {
            // 142168580 removes registration membership but leaves the actual
            // tick prefix and prerequisite backing alive. B owns the binding,
            // never the C values: capture current memory even when this tick
            // disappeared from every active/cooldown set during execution.
            // This also covers secondary ticks; assuming component+110 loses
            // the +7B0 tick owned by native registration handler141DA9000.
            for(const auto& tick:continuation_owner->ticks_) {
                const auto level=Field<std::uintptr_t>(tick.binding_layout,0x48);
                if(std::none_of(next.levels_.begin(),next.levels_.end(),[&](const auto& row){return row.address==level;})) {
                    status=Status::failure(FailureCode::GenerationMismatch);break;
                }
                status=next.CaptureTick(tick.address,level,budget-owned_bytes());
                if(!status.ok())break;
            }
        }
        if(status.ok() && retained.visit) {
            struct Context {Sc6ReplaySchedulerState* image;std::size_t budget;Status status;};
            Context context{&next,budget-owned_bytes(),Status::success()};
            status=retained.visit(retained.context,&context,[](void* opaque,std::uintptr_t component) {
                auto& c=*static_cast<Context*>(opaque);auto& image=*c.image;
                const auto tick=component+0x110;
                std::uintptr_t level{},owner{};
                if(!Read(tick+0x48,level) || !Read(tick+0x50,owner) || owner!=component
                    || std::none_of(image.levels_.begin(),image.levels_.end(),[&](const auto& row){return row.address==level;})) {
                    c.status=Status::failure(FailureCode::GenerationMismatch);return false;
                }
                // Retain the actual dormant prefix and prerequisites. The
                // native unregister routine142168580 leaves these intact;
                // registration membership remains represented by the sets.
                c.status=image.CaptureTick(tick,level,c.budget);
                return c.status.ok();
            });
            if(!context.status.ok()) status=context.status;
        }
        if (status.ok()) *this = std::move(next);
        return status;
    }
    catch (...) { return Status::failure(capacity); }
}

std::uint64_t Sc6ReplaySchedulerState::storage_fingerprint() const noexcept
{
    std::uint64_t result = 14695981039346656037ull;
    const auto add = [&](const auto& buffer) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(buffer.data());
        for (std::size_t i = 0; i < buffer.size() * sizeof(buffer[0]); ++i)
        { result ^= bytes[i]; result *= 1099511628211ull; }
    };
    for (const auto& level : levels_)
    {
        for (const auto& set : level.sets)
        { add(set.binding_layout); add(set.slots); add(set.allocation_flags); add(set.hash); }
        add(level.cooldown_order);
    }
    for (const auto& tick : ticks_) { add(tick.binding_layout); add(tick.prerequisites); }
    return result; // Diagnostic retention witness, not a canonical hash.
}
}
