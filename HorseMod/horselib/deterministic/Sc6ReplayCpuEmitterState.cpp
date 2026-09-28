#include "Sc6ReplayCpuEmitterState.hpp"
#include "ReplayGpuCompletion.hpp"
#include <Windows.h>
#include <cstring>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
SRWLOCK emitter_lifetime_lock = SRWLOCK_INIT;
template<class T> T Field(const void* data, std::size_t offset)
{ T result; std::memcpy(&result, static_cast<const std::byte*>(data) + offset, sizeof(result)); return result; }
bool Copy(void* target, const void* source, std::size_t bytes) noexcept
{
    if (!bytes) return true;
    if (!source || !target) return false;
    __try { std::memcpy(target, source, bytes); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Bind(std::uintptr_t base, std::uintptr_t object, std::array<std::int32_t, 2>& weak) noexcept
{
    __try
    {
        if (!object) return false;
        reinterpret_cast<void (*)(void*, const void*)>(base + 0xf7bad0)(weak.data(), reinterpret_cast<void*>(object));
        return reinterpret_cast<std::uintptr_t (*)(const void*)>(base + 0xf823f0)(weak.data()) == object;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool EmptyGpuConstructorStorage(const void* root) noexcept
{
    __try {
        for(const auto offset:{0xf0,0xf8,0x100,0x108,0x118,0x120,0x128,0x160,0x168,
            0x170,0x180,0x188,0x198,0x1a0,0x1a8,0x1c0,0x1c8,0x1e8,0x1f0,
            0x208,0x210,0x218,0x220,0x228,0x230,0x238,0x240,0x248,0x250,0x258})
            if(Field<std::uint64_t>(root,offset))return false;
        return !Field<std::uintptr_t>(root,0x10) && !Field<std::uintptr_t>(root,0x28);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}

bool Sc6ReplayCpuEmitterState::SpawnPerUnitPayloadMatches(std::uintptr_t base, const void* module, int bytes) noexcept
{
    __try {
        const auto table=Field<std::uintptr_t>(module,0);
        // Constructor141FB53D0 identifies this exact class. Size virtual returns
        // four; InitParameters141F9BFE0 zeroes the allocation before the no-op
        // initializer.141FCEE40 reads/writes one float of carried distance and
        // consumes emitter+50/+134 transforms to determine subsequent births.
        return bytes==4 && table==base+0x395ed20
            && Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x268)==base+0x301490
            && Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x270)==base+0x1e030c0
            && Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x328)==base+0x1fcee40;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayCpuEmitterState::GpuInstancePayloadMatches(std::uintptr_t base, const void* image) noexcept
{
    __try {
        if(!Field<std::uintptr_t>(image,0x100) && !Field<int>(image,0x108))return true;
        const auto* authored=Field<const void*>(image,0x10);
        const auto* modules=Field<const std::uintptr_t*>(authored,0x158);
        const auto* descriptor=Field<const void*>(image,0x1d8);
        // GPU spawn141F9A400 calls descriptor+10's virtual328. Admit only
        // that exact scalar consumer, not every CPU module payload class.
        // Base destructor141F8F9E0 owns +100; the ordinary Buffer graph below
        // clones/accounts it and retains B until the existing completion gate.
        return modules && Field<int>(authored,0x160)==1 && descriptor
            && Field<std::uintptr_t>(descriptor,0x10)==*modules
            && SpawnPerUnitPayloadMatches(base,reinterpret_cast<void*>(*modules),Field<int>(image,0x108))
            && InstancePayloadMatches(base,image,*modules);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Instance allocations contain only audited scalar state. Native lookup supplies
// each range; require disjoint, complete coverage, never infer sparse-map offsets.
// The only admitted two-module graph is Velocity_Seeded + Collision from Ice_Crack_01.
bool Sc6ReplayCpuEmitterState::InstancePayloadMatches(std::uintptr_t base, const void* image, std::uintptr_t module) noexcept
{
    __try {
        const auto data=Field<std::uintptr_t>(image,0x100);
        const auto bytes=Field<int>(image,0x108);
        if(!module) return !data && !bytes;
        const auto authored=Field<const void*>(image,0x10);
        const auto modules=Field<const std::uintptr_t*>(authored,0x158);
        const int count=Field<int>(authored,0x160);
        if(!data || bytes<=0 || bytes>20 || !modules || *modules!=module || count<1 || count>2) return false;
        std::array<std::uintptr_t,2> starts{};
        std::array<int,2> sizes{};
        int total{};
        for(int i=0;i<count;++i) {
            const auto* object=reinterpret_cast<const void*>(modules[i]);
            const auto table=Field<std::uintptr_t>(object,0);
            int size{};
            if(count==2 && table!=(base+(i==0?0x3961000:0x39573d8))) return false;
            if(table==base+0x3957d58) {
                // Native generators copy record+18's game-callback pointers
                // into per-tick rows; completion invokes their virtual230
                // after internal event receivers. Scalar payload ownership
                // does not own arbitrary callback state. Recheck authored
                // routes at capture and every instance publication boundary.
                const auto events=Field<const std::byte*>(object,0x30);
                const auto event_count=Field<int>(object,0x38),capacity=Field<int>(object,0x3c);
                if(event_count<0 || event_count>4096 || capacity<event_count || (capacity && !events)
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x268)!=base+0x3014b0
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x270)!=base+0x1fd3b90) return false;
                for(int event=0;event<event_count;++event) {
                    const auto* record=events+std::size_t(event)*0x28;
                    const auto callbacks=Field<std::uintptr_t>(record,0x18);
                    const auto count=Field<int>(record,0x20),reserved=Field<int>(record,0x24);
                    if(count!=0 || reserved<0 || (reserved && !callbacks))return false;
                }
                size=20;
            }
            else if(table==base+0x39573d8) {
                // +128 bit0 enables impulses into hit actors, outside this image.
                if((Field<std::uint32_t>(object,0x128)&1)
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x268)!=base+0x301490
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x270)!=base+0x1fd3b80) return false;
                size=4;
            } else if(table==base+0x3961000) {
                // RequiredBytes141FD7E90 returns eight iff authored seed count>0.
                // Init141FD3D90 ->141FA0850 writes InitialSeed/CurrentSeed.
                // Spawn141FDB3A0 ->141FDD210 consumes the stream for velocity;
                // Loop141FBDD10 may reset it. Restore bytes, never reinitialize.
                if(Field<int>(object,0xd8)<=0
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x238)!=base+0x1fdb3a0
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x268)!=base+0x1fd7e90
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x270)!=base+0x1fd3d90
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x2c8)!=base+0x1fbdd10
                    || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x308)!=base+0x1fa0850) return false;
                size=8;
            } else if(SpawnPerUnitPayloadMatches(base,object,4)) size=4;
            else return false;
            const auto start=reinterpret_cast<std::uintptr_t (*)(const void*,const void*)>(base+0x1f9a380)(image,object);
            if(start<data || size>bytes || start-data>static_cast<std::uintptr_t>(bytes-size)) return false;
            for(int j=0;j<i;++j) if(start<starts[j]+sizes[j] && starts[j]<start+size) return false;
            starts[i]=start;sizes[i]=size;total+=size;
        }
        return total==bytes;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Sc6ReplayCpuEmitterState::ModuleBindingsMatch(const void* image,std::uintptr_t first,std::uintptr_t second) noexcept
{
    __try {
        if(!first) return !second;
        const auto authored=Field<const void*>(image,0x10);
        const auto modules=Field<const std::uintptr_t*>(authored,0x158);
        return modules && Field<int>(authored,0x160)==(second?2:1)
            && modules[0]==first && (!second || modules[1]==second);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Factory 141F942F0 allocates 0x200; 141FAA9B0 updates the mesh payload in
// the ordinary particle buffer. 141F90870 frees +1E8, populated as a material
// override TArray by 141FAB350 and consumed by 141F9A0F0. Nonempty overrides
// need additional retained UObject ownership; do not copy their pointers.
bool Sc6ReplayCpuEmitterState::MeshPayloadSupported(const void* image) noexcept
{
    const auto stride = Field<int>(image, 0x114);
    const auto rotation = Field<int>(image, 0x1dc), previous = Field<int>(image, 0x1e0);
    return !Field<std::uintptr_t>(image, 0x1e8) && !Field<std::uint64_t>(image, 0x1f0)
        && Field<std::uint8_t>(image, 0x1d8) <= 1
        && stride >= 128 && stride <= 4096
        && ((rotation >= 128 && rotation <= stride - 0x48)
            || (!rotation && !Field<int>(image, 0x118) && !Field<int>(image, 0x120)))
        && (!previous || (previous >= 128 && previous <= stride - 0x38));
}
bool Sc6ReplayCpuEmitterState::MeshBindingsMatch(const void* image,
    std::uintptr_t type_data, std::uintptr_t mesh) noexcept
{
    __try {
        // 141F9C890 selects LOD zero's TypeData, not the mutable current LOD.
        const auto* authored = Field<const void*>(image, 0x10);
        if (!authored || Field<int>(authored, 0x40) <= 0 || !type_data || !mesh
            || Field<std::uintptr_t>(image, 0x1d0) != type_data) return false;
        const auto* lods = Field<const std::uintptr_t*>(authored, 0x38);
        return lods && *lods && Field<std::uintptr_t>(reinterpret_cast<void*>(*lods), 0x48) == type_data
            && Field<std::uintptr_t>(reinterpret_cast<void*>(type_data), 0x30) == mesh;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::size_t Sc6ReplayCpuEmitterState::owned_bytes() const noexcept
{
    auto bytes = sizeof(*this) + buffers_.capacity() * sizeof(Buffer);
    for (const auto& buffer : buffers_) bytes += buffer.bytes.capacity();
    return bytes;
}
std::uint64_t Sc6ReplayCpuEmitterState::storage_fingerprint() const noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](const void* source, std::size_t bytes) {
        const auto* data = static_cast<const unsigned char*>(source);
        for (std::size_t i = 0; i < bytes; ++i) { hash ^= data[i]; hash *= 1099511628211ull; }
    };
    add(object_.data(), object_bytes());
    add(&component_type_,sizeof(component_type_)); add(&component_template_,sizeof(component_template_));
    for (const auto& buffer : buffers_)
    {
        add(&buffer.parent, sizeof(buffer.parent)); add(&buffer.offset, sizeof(buffer.offset));
        add(buffer.bytes.data(), buffer.bytes.size());
    }
    return hash; // Retention witness, not an independent correctness oracle.
}
Status Sc6ReplayCpuEmitterState::AddBuffer(int parent, std::size_t offset, std::uintptr_t source,
    std::size_t bytes, std::size_t budget)
{
    if (owned_bytes() > budget || bytes > budget - owned_bytes()) return Status::failure(FailureCode::CapacityExceeded);
    if (bytes && !source) return Status::failure(FailureCode::CapturePreflightFailed);
    buffers_.push_back({parent, offset, {}});
    auto& buffer = buffers_.back();
    buffer.bytes.resize(bytes);
    if (owned_bytes() > budget) return Status::failure(FailureCode::CapacityExceeded);
    return Copy(buffer.bytes.data(), reinterpret_cast<void*>(source), bytes)
        ? Status::success() : Status::failure(FailureCode::CaptureFailed);
}
bool Sc6ReplayCpuEmitterState::GpuDescriptorBindingMatches(std::uintptr_t base,
    const void* image, std::uintptr_t type) noexcept
{
    // Native141F70FF0 selects asset LOD zero's TypeData(+48), even when the
    // instance's current LOD differs. GPU virtual338/141F94230 passes its
    // inline descriptor(+30) to FX virtual28/141F941E0. A matching field
    // asset alone cannot prove that descriptor's lifetime or provenance.
    __try {
        if(!type || type>UINTPTR_MAX-0x30
            || Field<std::uintptr_t>(reinterpret_cast<void*>(type),0)!=base+0x394b9e0
            || Field<std::uintptr_t>(image,0x1d8)!=type+0x30)return false;
        const auto* asset=Field<const void*>(image,0x10);
        if(!asset)return false;
        const auto count=Field<int>(asset,0x40);
        const auto* lods=Field<const std::uintptr_t*>(asset,0x38);
        if(count<1 || count>64 || !lods || !lods[0])return false;
        return Field<std::uintptr_t>(reinterpret_cast<void*>(lods[0]),0x48)==type;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Sc6ReplayCpuEmitterState::GpuFieldBindingMatches(const void* image, std::uintptr_t asset) noexcept
{
    // 141F8D370 calls descriptor+30's virtual230 to bind the local field.
    // Static implementation1421A31F0 borrows asset+58; asset BeginDestroy
    // 142193D90/1421AC880 owns render-thread resource destruction.
    __try {
        const auto descriptor=Field<std::uintptr_t>(image,0x1d8);
        return descriptor && Field<std::uintptr_t>(reinterpret_cast<void*>(descriptor),0x30)==asset;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
Status Sc6ReplayCpuEmitterState::CaptureUnchecked(std::uintptr_t base, void* component, void* emitter, std::size_t budget)
{
    base_ = base; thread_ = GetCurrentThreadId();
    if (!Copy(object_.data(), emitter, object_bytes())) return Status::failure(FailureCode::CaptureFailed);
    const auto* image = object_.data();
    if (Field<std::uintptr_t>(image, 0) != base + Vtable(kind_)
        || Field<void*>(image, 0x18) != component) return Status::failure(FailureCode::IdentityMismatch);
    if(!Copy(&component_type_,component,sizeof(component_type_))
        || !Copy(&component_template_,static_cast<const std::byte*>(component)+0x808,sizeof(component_template_)))
        return Status::failure(FailureCode::CaptureFailed);
    // Attached callbacks remain unsupported. Only the independently audited
    // scalar instance payloads below are admitted beyond the empty graph.
    if (Field<std::uintptr_t>(image, 0x1c0) || Field<int>(image, 0x1c8) || Field<int>(image, 0x1cc))
        return Status::failure(FailureCode::UnsupportedContent);
    constexpr std::array<std::size_t, 4> offsets{0x10, 0x18, 0x28, 0x1a0};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        auto& binding = bindings_[i]; binding.address = Field<std::uintptr_t>(image, offsets[i]);
        // Sprite/GPU use 141F981C0; mesh uses 141F982A0 -> 141F9A0F0.
        // Null +1A0 selects native authored mesh/LOD material fallback.
        if (i == 3 && !binding.address) continue;
        if (!Bind(base, binding.address, binding.weak)) return Status::failure(FailureCode::GenerationMismatch);
    }
    if (gpu_storage()) {
        const auto descriptor=Field<std::uintptr_t>(image,0x1d8);
        if(descriptor<0x30)return Status::failure(FailureCode::GenerationMismatch);
        bindings_[5].address=descriptor-0x30;
        if(!GpuDescriptorBindingMatches(base,image,bindings_[5].address)
            || !Bind(base,bindings_[5].address,bindings_[5].weak))
            return Status::failure(FailureCode::GenerationMismatch);
        if(!descriptor || !Copy(&bindings_[8].address,reinterpret_cast<void*>(descriptor+0x30),sizeof(std::uintptr_t))
            || (bindings_[8].address && !Bind(base,bindings_[8].address,bindings_[8].weak)))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    if (kind_ == Kind::Mesh) {
        if (!MeshPayloadSupported(image)) return Status::failure(FailureCode::UnsupportedContent);
        bindings_[5].address = Field<std::uintptr_t>(image, 0x1d0);
        if (!Bind(base, bindings_[5].address, bindings_[5].weak)
            || !Copy(&bindings_[6].address, reinterpret_cast<void*>(bindings_[5].address + 0x30), sizeof(std::uintptr_t))
            || !Bind(base, bindings_[6].address, bindings_[6].weak)
            || !MeshBindingsMatch(image, bindings_[5].address, bindings_[6].address))
            return Status::failure(FailureCode::GenerationMismatch);
    }
    if(Field<std::uintptr_t>(image,0x100) || Field<int>(image,0x108))
    {
        if(gpu_storage() && !GpuInstancePayloadMatches(base,image)) return Status::failure(FailureCode::UnsupportedContent);
        const auto* authored=reinterpret_cast<void*>(bindings_[0].address);
        const auto module_count=Field<int>(authored,0x160);
        if(module_count<1 || module_count>2) return Status::failure(FailureCode::UnsupportedContent);
        const auto modules=Field<const std::uintptr_t*>(authored,0x158);
        if(!modules) return Status::failure(FailureCode::UnsupportedContent);
        bindings_[4].address=*modules;
        if(module_count==2) {
            bindings_[7].address=modules[1];
            if(!Bind(base,bindings_[7].address,bindings_[7].weak)) return Status::failure(FailureCode::GenerationMismatch);
        }
        if(!Bind(base,bindings_[4].address,bindings_[4].weak) || !InstancePayloadMatches(base,image,bindings_[4].address))
            return Status::failure(FailureCode::UnsupportedContent);
    }
    if (!Copy(&lod_peak_, reinterpret_cast<void*>(bindings_[2].address + 0xb4), sizeof(lod_peak_)))
        return Status::failure(FailureCode::CaptureFailed);
    const auto capacity = Field<int>(image, 0x120), active = Field<int>(image, 0x118), stride = Field<int>(image, 0x114);
    const auto lods = Field<int>(image, 0x168), lod_capacity = Field<int>(image, 0x16c);
    const auto durations = Field<int>(image, 0x188), duration_capacity = Field<int>(image, 0x18c);
    if (capacity < 0 || capacity > 65535 || active < 0 || (gpu_storage() ? active > 1048576 : active > capacity) || stride < 128 || stride > 4096 || stride % 16
        || (capacity && !Field<std::uintptr_t>(image, 0xf8))
        || lods < 0 || lods > 32 || lod_capacity < lods || lod_capacity > 64
        || durations < 0 || duration_capacity < durations || duration_capacity > 64)
        return Status::failure(FailureCode::CapturePreflightFailed);
    // The audited GPU variant stores particles in the shared textures, not
    // CPU particle/index buffers. Do not apply CPU active<=capacity to it.
    if (gpu_storage() && (capacity || Field<std::uintptr_t>(image, 0xf0) || Field<std::uintptr_t>(image, 0xf8)))
        return Status::failure(FailureCode::UnsupportedContent);
    const auto buffer_count = static_cast<std::size_t>(lods) + 4 + (gpu_storage() ? 7 : 0) + (bindings_[4].address ? 1 : 0);
    if (sizeof(*this) > budget || buffer_count > (budget - sizeof(*this)) / sizeof(Buffer))
        return Status::failure(FailureCode::CapacityExceeded);
    buffers_.reserve(buffer_count);
    auto status = AddBuffer(-1, 0xf0, Field<std::uintptr_t>(image, 0xf0), static_cast<std::size_t>(capacity) * stride, budget);
    if (status.ok()) status = AddBuffer(-1, 0xf8, Field<std::uintptr_t>(image, 0xf8),
        Field<std::uintptr_t>(image, 0xf8) ? (static_cast<std::size_t>(capacity) + 1) * 2 : 0, budget);
    if (status.ok()) status = AddBuffer(-1, 0x160, Field<std::uintptr_t>(image, 0x160), static_cast<std::size_t>(lod_capacity) * 16, budget);
    if (status.ok()) status = AddBuffer(-1, 0x180, Field<std::uintptr_t>(image, 0x180), static_cast<std::size_t>(duration_capacity) * 4, budget);
    if (!status.ok()) return status;
    for (int i = 0; i < lods; ++i)
    {
        const auto* row = buffers_[2].bytes.data() + static_cast<std::size_t>(i) * 16;
        const auto count = Field<int>(row, 8), bytes = Field<int>(row, 12);
        if (count < 0 || bytes < count || bytes > 65536) return Status::failure(FailureCode::CapturePreflightFailed);
        status = AddBuffer(2, static_cast<std::size_t>(i) * 16, Field<std::uintptr_t>(row, 0), bytes, budget);
        if (!status.ok()) return status;
    }
    if(bindings_[4].address)
    {
        status=AddBuffer(-1,0x100,Field<std::uintptr_t>(image,0x100),static_cast<std::size_t>(Field<int>(image,0x108)),budget);
        if(!status.ok()) return status;
    }
    if (gpu_storage())
    {
        status = CaptureGpuBuffers(budget);
        if (!status.ok()) return status;
    }
    valid_ = true;
    return ValidateBindings();
}
Status Sc6ReplayCpuEmitterState::CaptureGpuBuffers(std::size_t budget)
{
    const auto* image = object_.data();
    const auto system = Field<std::uintptr_t>(image, 0x1d0), render = Field<std::uintptr_t>(image, 0x1e0);
    std::uintptr_t system_vt{}, render_vt{}, embedded_vt{};
    if (!system || !render || !Field<std::uintptr_t>(image, 0x1d8)
        || !Copy(&system_vt, reinterpret_cast<void*>(system), 8) || system_vt != base_ + 0x3941830
        || !Copy(&render_vt, reinterpret_cast<void*>(render), 8) || render_vt != base_ + 0x394bfc0
        || !Copy(&embedded_vt, reinterpret_cast<void*>(render + 0x1f0), 8) || embedded_vt != base_ + 0x394c000)
        return Status::failure(FailureCode::GenerationMismatch);
    const auto tiles = Field<int>(image, 0x1f0), bits = Field<int>(image, 0x210), max_bits = Field<int>(image, 0x214);
    if (tiles < 0 || tiles > 65536 || bits != tiles || Field<int>(image, 0x220) != tiles
        || max_bits < bits || max_bits > 65536 || (!Field<std::uintptr_t>(image, 0x208) && max_bits > 128))
        return Status::failure(FailureCode::CapturePreflightFailed);
    // 141F8F920 frees all seven extension backings; 141F97F80 compacts
    // tile IDs, activity bits and per-tile scalars together. Pending new-tile
    // and spawn rows must survive independently of those persistent arrays.
    constexpr std::array<std::size_t, 6> offsets{0x1e8, 0x218, 0x228, 0x238, 0x248, 0x258};
    constexpr std::array<std::size_t, 6> strides{4, 4, 4, 0x48, 0x48, 0x48};
    for (std::size_t i = 0; i < offsets.size(); ++i)
    {
        const auto offset = offsets[i];
        const auto count = Field<int>(image, offset + 8), capacity = Field<int>(image, offset + 12);
        if (count < 0 || capacity < count || capacity > 1048576 || (i < 3 && count > 65536))
            return Status::failure(FailureCode::CapturePreflightFailed);
        const auto status = AddBuffer(-1, offset, Field<std::uintptr_t>(image, offset),
            static_cast<std::size_t>(capacity) * strides[i], budget);
        if (!status.ok()) return status;
    }
    const auto heap = Field<std::uintptr_t>(image, 0x208);
    return AddBuffer(-1, 0x208, heap, heap ? (static_cast<std::size_t>(max_bits) + 31) / 32 * 4 : 0, budget);
}

Status Sc6ReplayCpuEmitterState::Capture(std::uintptr_t base, void* component, void* emitter, std::size_t budget) noexcept
{
    return CaptureImpl(base, component, emitter, budget, false);
}
Status Sc6ReplayCpuEmitterState::CaptureGpu(std::uintptr_t base, void* component, void* emitter, std::size_t budget) noexcept
{
    return CaptureImpl(base, component, emitter, budget, true);
}
Status Sc6ReplayCpuEmitterState::CaptureImpl(std::uintptr_t base, void* component, void* emitter, std::size_t budget, bool gpu) noexcept
{
    if (!base || !component || !emitter || base != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)))
        return Status::failure(FailureCode::IdentityMismatch);
    if (owned_bytes() >= budget) return Status::failure(FailureCode::CapacityExceeded);
    try
    {
        Sc6ReplayCpuEmitterState next;
        std::uintptr_t table{};
        if (!Copy(&table, emitter, sizeof(table))) return Status::failure(FailureCode::CaptureFailed);
        next.kind_ = gpu ? Kind::Gpu : table == base + Vtable(Kind::Mesh) ? Kind::Mesh : Kind::Sprite;
        auto status = next.CaptureUnchecked(base, component, emitter, budget - owned_bytes());
        if (status.ok()) *this = std::move(next);
        return status;
    }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
}
Status Sc6ReplayCpuEmitterState::ValidateBindings(ValidationFailure* failure) const noexcept
{
    return ValidateBindingsForComponent(nullptr,failure);
}

Status Sc6ReplayCpuEmitterState::ValidateBindingsForComponent(const ComponentReplacement* component, ValidationFailure* failure) const noexcept
{
    if (!valid_ || thread_ != GetCurrentThreadId()) return Status::failure(FailureCode::IllegalTransition);
    __try
    {
        if(component) {
            const auto& old=bindings_[1];
            // The cold-copy construction is verified only for this exact native
            // Lux particle class. Other component subclasses retain the old
            // lifetime rejection until their construction path is established.
            if(component->source!=old.address || component->source_weak!=old.weak
                || !component->target || component->target==old.address || component->target_weak==old.weak
                || component_type_!=base_+0x335db28 || !component_template_
                || reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(component->target_weak.data())!=component->target
                || Field<std::uintptr_t>(reinterpret_cast<void*>(component->target),0)!=component_type_
                || Field<std::uintptr_t>(reinterpret_cast<void*>(component->target),0x808)!=component_template_) {
                if(failure)failure->check="replacement_component_identity";
                return Status::failure(FailureCode::GenerationMismatch);
            }
        }
        for (std::size_t i=0;i<bindings_.size();++i) {
            if(component && i==1)continue; // Only this explicitly matched typed edge is translated.
            const auto& binding=bindings_[i];
            const auto actual=binding.address?reinterpret_cast<std::uintptr_t (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data()):0;
            if(actual!=binding.address) {
                if(failure){failure->check="weak_binding";failure->offset=i;failure->expected=binding.address;failure->actual=actual;}
                return Status::failure(FailureCode::GenerationMismatch);
            }
        }
        if(gpu_storage() && !GpuDescriptorBindingMatches(base_,object_.data(),bindings_[5].address)) {
            if(failure)failure->check="gpu_descriptor_owner";
            return Status::failure(FailureCode::GenerationMismatch);
        }
        if(gpu_storage() && !GpuFieldBindingMatches(object_.data(),bindings_[8].address)) {
            if(failure)failure->check="gpu_vector_field_asset";
            return Status::failure(FailureCode::GenerationMismatch);
        }
        if (kind_ == Kind::Mesh && (!MeshPayloadSupported(object_.data())
            || !MeshBindingsMatch(object_.data(), bindings_[5].address, bindings_[6].address))) {
            if(failure)failure->check="mesh_payload_binding";
            return Status::failure(FailureCode::GenerationMismatch);
        }
        if((gpu_storage() && !GpuInstancePayloadMatches(base_,object_.data()))
            || !ModuleBindingsMatch(object_.data(),bindings_[4].address,bindings_[7].address)
            || !InstancePayloadMatches(base_,object_.data(),bindings_[4].address)) {
            if(failure)failure->check="instance_payload_binding";
            return Status::failure(FailureCode::GenerationMismatch);
        }
        // Resize can update this shared authored-LOD cache. Do not silently
        // treat that external write as covered by the emitter image.
        const auto peak=Field<int>(reinterpret_cast<void*>(bindings_[2].address), 0xb4);
        if (peak != lod_peak_) {
            if(failure){failure->check="shared_lod_peak";failure->offset=0xb4;failure->expected=lod_peak_;failure->actual=peak;}
            return Status::failure(FailureCode::GenerationMismatch);
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Sc6ReplayCpuEmitterState::Prepared* Sc6ReplayCpuEmitterState::Prepared::lifetime_head_{};
bool Sc6ReplayCpuEmitterState::Prepared::HasExecutionOwners() noexcept
{
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const bool result=lifetime_head_!=nullptr;
    ReleaseSRWLockShared(&emitter_lifetime_lock);return result;
}
void Sc6ReplayCpuEmitterState::Prepared::ForgetExecutionOwner() noexcept
{
    AcquireSRWLockExclusive(&emitter_lifetime_lock);
    auto** link=&lifetime_head_;
    while(*link && *link!=this)link=&(*link)->lifetime_next_;
    if(*link)*link=lifetime_next_;
    lifetime_registered_=false;lifetime_next_=nullptr;
    ReleaseSRWLockExclusive(&emitter_lifetime_lock);
}
bool Sc6ReplayCpuEmitterState::Prepared::native_destroyed() const noexcept
{
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const bool result=native_lifetime_==NativeLifetime::Destroyed;
    ReleaseSRWLockShared(&emitter_lifetime_lock);return result;
}
std::uint32_t Sc6ReplayCpuEmitterState::Prepared::destruction_diagnostic() const noexcept
{
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const auto result=static_cast<unsigned>(native_lifetime_) | (destruction_entry_failure_<<2)
        | (destruction_entries_?0x100u:0u) | (destruction_returns_?0x200u:0u)
        | (destruction_entries_>1?0x400u:0u) | (destruction_returns_>1?0x800u:0u);
    ReleaseSRWLockShared(&emitter_lifetime_lock);return result;
}
unsigned Sc6ReplayCpuEmitterState::Prepared::DestructionEntryFailure(bool mesh,unsigned flags) const noexcept
{
    if(thread_!=GetCurrentThreadId())return 1;
    if(!executing_)return 2;
    if(execution_settled_)return 3;
    if(!published_)return 4;
    if(gpu_storage())return 5;
    if((kind_==Kind::Mesh)!=mesh)return 6;
    if(flags!=1)return 7;
    __try {
        const auto* component=reinterpret_cast<void*>(bindings_[1].address);
        if(Field<std::uintptr_t>(object_,0)!=base_+Vtable(kind_))return 8;
        if(Field<std::uintptr_t>(object_,0x18)!=bindings_[1].address)return 9;
        if(Field<std::uintptr_t>(component,0xa50)!=execution_slots_)return 10;
        if(Field<int>(component,0xa58)!=execution_count_)return 11;
        if(Field<int>(component,0xa5c)!=execution_capacity_)return 12;
        if(Field<void*>(reinterpret_cast<void*>(execution_slots_),ordinal_*sizeof(void*))!=object_)return 13;
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 14;}
}
void Sc6ReplayCpuEmitterState::Prepared::NativeDestruction(void* root,bool mesh,unsigned flags,bool completed) noexcept
{
    AcquireSRWLockExclusive(&emitter_lifetime_lock);
    for(auto* owner=lifetime_head_;owner;owner=owner->lifetime_next_) {
        // Native allocation addresses can be reused within this seven-tick tail.
        // A completed receipt belongs to the retired allocation, not whatever
        // later occupies its address. Settlement still verifies original owner,
        // null ordinal, B storage and required component/GPU completion.
        if(owner->object_!=root || owner->native_lifetime_==NativeLifetime::Destroyed)continue;
        if(completed) {
            if(owner->destruction_returns_<2)++owner->destruction_returns_;
            // The native root/backing have already been freed. Read only our
            // witness, never the allocation or a peer-notification parameter.
            owner->native_lifetime_=owner->native_lifetime_==NativeLifetime::Destroying
                && owner->thread_==GetCurrentThreadId() && flags==1 && (owner->kind_==Kind::Mesh)==mesh
                ? NativeLifetime::Destroyed : NativeLifetime::Invalid;
        } else {
            if(owner->destruction_entries_<2)++owner->destruction_entries_;
            const auto failure=owner->native_lifetime_==NativeLifetime::Live
                ? owner->DestructionEntryFailure(mesh,flags) : 15u;
            if(!owner->destruction_entry_failure_)owner->destruction_entry_failure_=failure;
            owner->native_lifetime_=failure==0 ? NativeLifetime::Destroying : NativeLifetime::Invalid;
        }
    }
    ReleaseSRWLockExclusive(&emitter_lifetime_lock);
}
bool Sc6ReplayCpuEmitterState::Prepared::ValidateGpuDestructionEntry(unsigned flags) const noexcept
{
    if (thread_ != GetCurrentThreadId() || !executing_ || execution_settled_ || !published_
        || !gpu_storage() || !component_replaced_ || !gpu_target_ || displaced_ || flags != 1) return false;
    __try {
        const auto* component = reinterpret_cast<void*>(bindings_[1].address);
        return Field<std::uintptr_t>(gpu_target_, 0) == base_ + Vtable(Kind::Gpu)
            && Field<std::uintptr_t>(gpu_target_, 0x18) == bindings_[1].address
            && execution_slots_ && ordinal_ < static_cast<std::size_t>(execution_count_)
            && Field<std::uintptr_t>(component, 0xa50) == execution_slots_
            && Field<int>(component, 0xa58) == execution_count_
            && Field<int>(component, 0xa5c) == execution_capacity_
            && Field<void*>(reinterpret_cast<void*>(execution_slots_), ordinal_ * sizeof(void*)) == gpu_target_;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Sc6ReplayCpuEmitterState::Prepared::NativeGpuDestruction(void* root, unsigned flags, bool completed) noexcept
{
    AcquireSRWLockExclusive(&emitter_lifetime_lock);
    for (auto* owner = lifetime_head_; owner; owner = owner->lifetime_next_) {
        // The private header is never the root native code destroys.
        if (!owner->gpu_storage() || !owner->component_replaced_ || owner->gpu_target_ != root) continue;
        if (completed) {
            owner->native_lifetime_ = owner->native_lifetime_ == NativeLifetime::Destroying
                && owner->thread_ == GetCurrentThreadId() && flags == 1
                ? NativeLifetime::Destroyed : NativeLifetime::Invalid;
            if (owner->fresh_gpu_completion_)
                owner->native_death_submission_ = owner->fresh_gpu_completion_->submitted_serial();
        } else {
            owner->native_lifetime_ = owner->native_lifetime_ == NativeLifetime::Live
                && owner->ValidateGpuDestructionEntry(flags) ? NativeLifetime::Destroying : NativeLifetime::Invalid;
        }
    }
    ReleaseSRWLockExclusive(&emitter_lifetime_lock);
}

bool Sc6ReplayCpuEmitterState::Prepared::ValidateComponentDestructionEntry() const noexcept
{
    if (!component_replaced_ || !executing_ || execution_settled_ || !published_
        || displaced_ || thread_ != GetCurrentThreadId()) return false;
    __try {
        return bindings_[1].address && reinterpret_cast<std::uintptr_t(*)(const void*)>(base_ + 0xf823f0)
            (bindings_[1].weak.data()) == bindings_[1].address;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Sc6ReplayCpuEmitterState::Prepared::NativeComponentDestruction(void* component, bool completed) noexcept
{
    AcquireSRWLockExclusive(&emitter_lifetime_lock);
    for (auto* owner = lifetime_head_; owner; owner = owner->lifetime_next_) {
        if (!owner->component_replaced_
            || owner->bindings_[1].address != reinterpret_cast<std::uintptr_t>(component)) continue;
        if (completed) {
            // Native component teardown may have freed the array, invalidated
            // the weak identity and queued the proxy. Read only our journal.
            owner->component_lifetime_ = owner->component_lifetime_ == NativeLifetime::Destroying
                && owner->thread_ == GetCurrentThreadId() ? NativeLifetime::Destroyed : NativeLifetime::Invalid;
            if (owner->fresh_gpu_completion_)
                owner->native_death_submission_ = owner->fresh_gpu_completion_->submitted_serial();
        } else {
            owner->component_lifetime_ = owner->component_lifetime_ == NativeLifetime::Live
                && owner->ValidateComponentDestructionEntry() ? NativeLifetime::Destroying : NativeLifetime::Invalid;
        }
    }
    ReleaseSRWLockExclusive(&emitter_lifetime_lock);
}

bool Sc6ReplayCpuEmitterState::Prepared::FreshComponentDestructionWitness(std::uintptr_t component,
    const std::array<std::int32_t,2>& weak) const noexcept
{
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const bool result = component_replaced_ && executing_ && published_ && !displaced_
        && thread_ == GetCurrentThreadId() && bindings_[1].address == component && bindings_[1].weak == weak
        && native_lifetime_ == NativeLifetime::Destroyed && component_lifetime_ == NativeLifetime::Destroyed;
    ReleaseSRWLockShared(&emitter_lifetime_lock);
    return result;
}

bool Sc6ReplayCpuEmitterState::Prepared::FreshGpuDestructionWitness() const noexcept
{
    if(!gpu_storage())return false;
    if(FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak))return true;
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const bool live_component=component_replaced_ && gpu_storage() && executing_ && published_ && !displaced_
        && thread_==GetCurrentThreadId() && native_lifetime_==NativeLifetime::Destroyed
        && component_lifetime_==NativeLifetime::Live;
    ReleaseSRWLockShared(&emitter_lifetime_lock);
    if(!live_component)return false;
    __try {
        const auto& binding=bindings_[1];
        if(!binding.address || reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(binding.weak.data())!=binding.address)return false;
        const auto* component=reinterpret_cast<const void*>(binding.address);
        const auto fence=Field<std::uintptr_t>(component,0xa70);
        if((Field<unsigned>(component,0x188)&0xc00000e0u) || Field<unsigned char>(component,0xa81)
            || (fence && !(Field<std::uint64_t>(reinterpret_cast<const void*>(fence),8)&(1ull<<26)))
            || !execution_slots_ || ordinal_>=static_cast<std::size_t>(execution_count_)
            || Field<std::uintptr_t>(component,0xa50)!=execution_slots_
            || Field<int>(component,0xa58)!=execution_count_ || Field<int>(component,0xa5c)!=execution_capacity_)return false;
        // Native completion notified peers and cleared this slot after the
        // deleting destructor. Never inspect gpu_target_ or the freed render.
        return Field<void*>(reinterpret_cast<const void*>(execution_slots_),ordinal_*sizeof(void*))==nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool Sc6ReplayCpuEmitterState::Prepared::FreshComponentRetirementWitness(std::uintptr_t component,
    const std::array<std::int32_t,2>& weak) const noexcept
{
    return FreshComponentDestructionWitness(component, weak) && fresh_gpu_retired_ && execution_settled_;
}

void* volatile* Sc6ReplayCpuEmitterState::Prepared::ResolveDestroyedSlot() const noexcept
{
    if(!native_destroyed() || thread_!=GetCurrentThreadId() || !executing_ || !published_ || gpu_storage())return nullptr;
    __try {
        for(const auto& binding:bindings_)
            if(binding.address && reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(binding.weak.data())!=binding.address)return nullptr;
        if(Field<int>(reinterpret_cast<void*>(bindings_[2].address),0xb4)!=lod_peak_)return nullptr;
        const auto* component=reinterpret_cast<void*>(bindings_[1].address);
        const auto fence=Field<std::uintptr_t>(component,0xa70);
        if(Field<std::uint8_t>(component,0xa81)
            || (fence && !(Field<std::uint64_t>(reinterpret_cast<void*>(fence),8)&(1ull<<26)))
            || Field<std::uintptr_t>(component,0xa50)!=execution_slots_
            || Field<int>(component,0xa58)!=execution_count_
            || Field<int>(component,0xa5c)!=execution_capacity_
            || !execution_slots_ || ordinal_>=static_cast<std::size_t>(execution_count_))return nullptr;
        auto* slot=reinterpret_cast<void* volatile*>(execution_slots_+ordinal_*sizeof(void*));
        return *slot==nullptr?slot:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}
Status Sc6ReplayCpuEmitterState::Prepared::SettleFreshGpuDestruction() noexcept
{
    if (!FreshGpuDestructionWitness()
        || allocations_.size() != 1 || allocations_[0].address != object_ || !object_)
        return Status::failure(FailureCode::GenerationMismatch);
    if (!fresh_gpu_retired_ && (!fresh_gpu_completion_ || !fresh_gpu_completion_->retired()
        || fresh_gpu_completion_->result() != ReplayGpuCompletion::Result::Complete
        || fresh_gpu_completion_->submitted_serial() <= native_death_submission_))
        return Status::failure(FailureCode::IllegalTransition);
    // The enclosing owner submitted the event on the native ordered render/RHI
    // route after C teardown. Native root/component receipts are distinct from
    // that event. Retain the private header until both contracts have completed.
    __try {
        for (std::size_t i = 0; i < bindings_.size(); ++i) {
            if (i == 1) continue; // Its exact native destruction was witnessed.
            const auto& binding = bindings_[i];
            if (binding.address && reinterpret_cast<std::uintptr_t(*)(const void*)>(base_ + 0xf823f0)
                (binding.weak.data()) != binding.address) return Status::failure(FailureCode::GenerationMismatch);
        }
        if (Field<int>(reinterpret_cast<void*>(bindings_[2].address), 0xb4) != lod_peak_)
            return Status::failure(FailureCode::GenerationMismatch);
        published_fingerprint_ = BackingFingerprint();
        fresh_gpu_retired_ = true; execution_settled_ = true;
        return ValidatePublished();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::Prepared::SettleFreshCpuComponentDestruction() noexcept
{
    if(gpu_storage() || !FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak))
        return Status::failure(FailureCode::GenerationMismatch);
    if(!fresh_gpu_retired_ && (!fresh_gpu_completion_ || !fresh_gpu_completion_->retired()
        || fresh_gpu_completion_->result()!=ReplayGpuCompletion::Result::Complete
        || fresh_gpu_completion_->submitted_serial()<=native_death_submission_))
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        // Only the exact component and root may be dead. Retained template,
        // LOD and module bindings remain independently validated. Never read
        // the freed CPU root, its buffers, or the destroyed component array.
        for(std::size_t i=0;i<bindings_.size();++i) {
            if(i==1)continue;
            const auto& binding=bindings_[i];
            if(binding.address && reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)
                (binding.weak.data())!=binding.address)return Status::failure(FailureCode::GenerationMismatch);
        }
        if(Field<int>(reinterpret_cast<void*>(bindings_[2].address),0xb4)!=lod_peak_)
            return Status::failure(FailureCode::GenerationMismatch);
        allocations_.clear();published_fingerprint_=BackingFingerprint();
        fresh_gpu_retired_=true;execution_settled_=true;
        return ValidatePublished();
    } __except(EXCEPTION_EXECUTE_HANDLER){return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayCpuEmitterState::Prepared::SettleNativeDestruction(const Sc6ReplayCpuEmitterState* original) noexcept
{
    if (gpu_storage()) return original ? Status::failure(FailureCode::GenerationMismatch) : SettleFreshGpuDestruction();
    if(FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak))
        return original?Status::failure(FailureCode::GenerationMismatch):SettleFreshCpuComponentDestruction();
    if(!ResolveDestroyedSlot() || (!original && displaced_))return Status::failure(FailureCode::GenerationMismatch);
    // Validate B before removing stale C bookkeeping. Native deletion, not
    // this transaction, retired C's graph. No pointer here may be freed twice.
    if(original) {
        if(original->base_!=base_ || original->kind_!=kind_)return Status::failure(FailureCode::GenerationMismatch);
        const auto status=original->ValidateNativeGraph(displaced_);if(!status.ok())return status;
    }
    allocations_.clear();published_fingerprint_=BackingFingerprint();execution_settled_=true;
    return original?original->ValidateDisplaced(*this):ValidatePublished();
}
Sc6ReplayCpuEmitterState::Prepared::~Prepared() { Clear(); }
void Sc6ReplayCpuEmitterState::Prepared::Clear() noexcept
{
    ForgetExecutionOwner();
    // Never reclaim backing still reachable through the native component.
    // Explicit undo/commit is required. Abandonment intentionally retains
    // native allocations as failure containment, not successful cleanup.
    if (published_) return;
    for (auto& allocation : allocations_)
        if (allocation.address) reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(allocation.address);
    allocations_.clear(); object_ = nullptr;
    executing_=execution_settled_=false;execution_budget_=0;
}
std::size_t Sc6ReplayCpuEmitterState::Prepared::owned_bytes() const noexcept
{
    auto bytes = sizeof(*this) + allocations_.capacity() * sizeof(Allocation) + execution_budget_;
    for (const auto& allocation : allocations_) bytes += allocation.charged;
    return bytes;
}
void* Sc6ReplayCpuEmitterState::Prepared::Allocate(std::size_t bytes, std::size_t budget)
{
    if (!bytes) { allocations_.push_back({}); return nullptr; }
    const auto charged = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base_ + 0xd50dc0)(bytes, 0);
    const auto used = owned_bytes();
    if (charged < bytes || used > budget || charged > budget - used) return nullptr;
    auto* memory = reinterpret_cast<void* (*)(std::size_t)>(base_ + 0x4a61c0)(bytes);
    if (!memory) return nullptr;
    allocations_.push_back({memory, charged, bytes});
    return memory;
}
std::uint64_t Sc6ReplayCpuEmitterState::Prepared::BackingFingerprint() const noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& allocation : allocations_)
    {
        const auto* bytes = static_cast<const unsigned char*>(allocation.address);
        for (std::size_t i = 0; i < allocation.requested; ++i)
        { hash ^= bytes[i]; hash *= 1099511628211ull; }
    }
    return hash; // Held-storage integrity only, never a native comparison oracle.
}

void* volatile* Sc6ReplayCpuEmitterState::Prepared::ResolveSlot(bool settling) const noexcept
{
    if (!object_ || thread_ != GetCurrentThreadId()) return nullptr;
    AcquireSRWLockShared(&emitter_lifetime_lock);
    const bool live=native_lifetime_==NativeLifetime::Live;
    ReleaseSRWLockShared(&emitter_lifetime_lock);
    if(!live)return nullptr;
    if(executing_ && !execution_settled_ && !settling)return nullptr;
    __try
    {
        // An accidental native advance may have reallocated particle/burst
        // backing without changing the component's emitter pointer. Refuse
        // undo/commit rather than later freeing retired allocation addresses.
        // Only settlement can inspect the stable component/slot lease without
        // old A allocation addresses. It separately verifies the complete C
        // image and builds new ownership metadata before undo/commit admission.
        if(!(executing_ && !execution_settled_ && settling))for (const auto& allocation : allocations_)
            if (allocation.pointer_slot && Field<void*>(allocation.pointer_slot, 0) != allocation.address)
                return nullptr;
        for (const auto& binding : bindings_)
            if (binding.address && reinterpret_cast<std::uintptr_t (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data()) != binding.address)
                return nullptr;
        if ((gpu_storage() && (!GpuDescriptorBindingMatches(base_,object_,bindings_[5].address)
                || !GpuFieldBindingMatches(object_,bindings_[8].address)
                || !GpuInstancePayloadMatches(base_,object_)))
            || Field<int>(reinterpret_cast<void*>(bindings_[2].address), 0xb4) != lod_peak_
            || !ModuleBindingsMatch(object_,bindings_[4].address,bindings_[7].address)
            || !InstancePayloadMatches(base_,object_,bindings_[4].address)) return nullptr;
        if (kind_ == Kind::Mesh && (!MeshPayloadSupported(object_)
            || !MeshBindingsMatch(object_, bindings_[5].address, bindings_[6].address))) return nullptr;
        const auto* component = reinterpret_cast<void*>(bindings_[1].address);
        const auto fence = Field<std::uintptr_t>(component, 0xa70);
        if (Field<std::uint8_t>(component, 0xa81)
            || (fence && !(Field<std::uint64_t>(reinterpret_cast<void*>(fence), 8) & (1ull << 26)))) return nullptr;
        const auto count = Field<int>(component, 0xa58), capacity = Field<int>(component, 0xa5c);
        const auto slots = Field<std::uintptr_t>(component, 0xa50);
        if (!slots || slots % alignof(void*) || count < 0 || count > 128 || capacity < count
            || ordinal_ >= static_cast<std::size_t>(count)) return nullptr;
        return reinterpret_cast<void* volatile*>(slots + ordinal_ * sizeof(void*));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

Status Sc6ReplayCpuEmitterState::Prepared::ValidateDestination(std::size_t ordinal, void* expected) noexcept
{
    if (published_ || !object_ || expected == object_) return Status::failure(FailureCode::IllegalTransition);
    ordinal_ = ordinal;
    auto* slot = ResolveSlot();
    if (!slot) return Status::failure(FailureCode::GenerationMismatch);
    __try
    {
        if (*slot != expected || (expected && (Field<std::uintptr_t>(expected, 0) != base_ + Vtable(kind_)
            || Field<std::uintptr_t>(expected, 0x18) != bindings_[1].address)))
            return Status::failure(FailureCode::GenerationMismatch);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::Prepared::ValidateGpuDestination(std::size_t ordinal, void* expected) noexcept
{
    if (!gpu_storage() || !expected) return Status::failure(FailureCode::IllegalTransition);
    const auto ready = ValidateDestination(ordinal, expected);
    if (!ready.ok()) return ready;
    __try
    {
        // B's common graph must satisfy the same bounded ownership contract
        // as A. Only the audited scalar module graph can use ordinary buffer
        // ownership; attached callback objects still need lifecycle participants.
        if (!GpuInstancePayloadMatches(base_,expected)
            || Field<std::uintptr_t>(expected, 0x1c0) || Field<int>(expected, 0x1c8) || Field<int>(expected, 0x1cc)
            || Field<std::uintptr_t>(expected, 0xf0) || Field<std::uintptr_t>(expected, 0xf8)
            || Field<int>(expected, 0x120))
            return Status::failure(FailureCode::UnsupportedContent);
        // System/descriptor are immutable bindings for this transaction. The
        // render allocation belongs to the current native emitter and may be
        // different from A's now-retired resource. Do not dereference A's copy.
        for (const auto offset : {0x1d0, 0x1d8})
            if (!Field<void*>(expected, offset) || Field<void*>(expected, offset) != Field<void*>(object_, offset))
                return Status::failure(FailureCode::GenerationMismatch);
        const auto* render = Field<void*>(expected, 0x1e0);
        if (!render || Field<std::uintptr_t>(render, 0) != base_ + 0x394bfc0
            || Field<std::uintptr_t>(render, 0x1f0) != base_ + 0x394c000
            || Field<std::uint8_t>(render, 0x252))
            return Status::failure(FailureCode::GenerationMismatch);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

void Sc6ReplayCpuEmitterState::Prepared::RebindRootSlots(void* from, void* to) noexcept
{
    const auto first = reinterpret_cast<std::uintptr_t>(from);
    for (auto& allocation : allocations_)
    {
        const auto slot = reinterpret_cast<std::uintptr_t>(allocation.pointer_slot);
        if (slot >= first && slot - first < gpu_undo_.size())
            allocation.pointer_slot = static_cast<std::byte*>(to) + (slot - first);
    }
}

Status Sc6ReplayCpuEmitterState::Prepared::PublishGpuStorage(std::size_t ordinal, void* expected) noexcept
{
    const auto ready = ValidateGpuDestination(ordinal, expected);
    if (!ready.ok()) return ready;
    if (!Copy(gpu_undo_.data(), expected, gpu_undo_.size())) return Status::failure(FailureCode::CaptureFailed);
    // Both graphs stay allocated. A is independently prepared; B's backing
    // remains reachable from gpu_undo_ until an acknowledged undo. No native
    // destructor, registration change, tile return or render builder is called.
    const auto render = Field<void*>(gpu_undo_.data(), 0x1e0);
    std::memcpy(static_cast<std::byte*>(object_) + 0x1e0, &render, sizeof(render));
    gpu_target_ = expected;
    published_fingerprint_ = BackingFingerprint();
    published_ = true; // A partial write is dirty too; preserve all undo owners.
    RebindRootSlots(object_, gpu_target_);
    gpu_write_complete_ = Copy(gpu_target_, object_, gpu_undo_.size());
    auto status = gpu_write_complete_ ? ValidatePublished() : Status::failure(FailureCode::RestoreWriteFailed);
    if (status.ok()) return status;
    return UndoPublication().ok() ? status : Status::failure(FailureCode::UndoFailed);
}

Status Sc6ReplayCpuEmitterState::Prepared::Publish(std::size_t ordinal, void* expected) noexcept
{
    // GPU publication requires separately owned render storage and registry
    // handoff. A storage clone may never transfer the captured raw resource.
    if (gpu_storage()) return Status::failure(FailureCode::UnsupportedContent);
    const auto ready = ValidateDestination(ordinal, expected);
    if (!ready.ok()) return ready;
    auto* slot = ResolveSlot();
    if (!slot) return Status::failure(FailureCode::GenerationMismatch);
    __try
    {
        published_fingerprint_ = BackingFingerprint();
        if (InterlockedCompareExchangePointer(slot, object_, expected) != expected)
            return Status::failure(FailureCode::GenerationMismatch);
        displaced_ = expected;
        published_ = true;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::Prepared::ValidatePublished() const noexcept
{
    if (!published_) return Status::failure(FailureCode::IllegalTransition);
    if (gpu_storage() && native_destroyed()) return fresh_gpu_retired_ && execution_settled_
        && allocations_.size() == 1 && allocations_[0].address == object_
        && FreshGpuDestructionWitness()
        && BackingFingerprint() == published_fingerprint_
        ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
    if(native_destroyed())return execution_settled_ && allocations_.empty()
        && (FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak)?fresh_gpu_retired_:ResolveDestroyedSlot()!=nullptr)
        ? Status::success():Status::failure(FailureCode::GenerationMismatch);
    auto* slot = ResolveSlot();
    if (!slot) return Status::failure(FailureCode::GenerationMismatch);
    __try
    {
        const auto* target = gpu_target_ ? gpu_target_ : object_;
        if (InterlockedCompareExchangePointer(slot, nullptr, nullptr) != target)
            return Status::failure(FailureCode::GenerationMismatch);
        if ((gpu_target_ && std::memcmp(gpu_target_, object_, gpu_undo_.size()))
            || BackingFingerprint() != published_fingerprint_)
            return Status::failure(FailureCode::RestoreVerificationFailed);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::Prepared::UndoPublication() noexcept
{
    if (!published_) return Status::success();
    if (native_destroyed() && (gpu_storage() || FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak))) {
        const auto retired = ValidatePublished(); if (!retired.ok()) return retired;
        // Complete B is a separate component graph. Never restore a constructor
        // header into C's freed root, nor write a cleared slot in a dead owner.
        ForgetExecutionOwner(); published_ = false; gpu_target_ = nullptr;
        if(!gpu_storage())object_=nullptr; // Native deletion already freed the CPU graph.
        return Status::success();
    }
    if (gpu_target_)
    {
        // Do not use ResolveSlot's installed-backing checks to reject recovery
        // after a partial native write. Validate the component's destination
        // independently, restore B, verify, then rebase A's ownership metadata.
        if (gpu_write_complete_ && !gpu_undo_started_ && !ResolveSlot())
            return Status::failure(FailureCode::GenerationMismatch);
        __try
        {
            if (thread_ != GetCurrentThreadId()) return Status::failure(FailureCode::IllegalTransition);
            for (const auto& binding : bindings_)
                if (binding.address && reinterpret_cast<std::uintptr_t (*)(const void*)>(base_ + 0xf823f0)(binding.weak.data()) != binding.address)
                    return Status::failure(FailureCode::GenerationMismatch);
            const auto* component = reinterpret_cast<void*>(bindings_[1].address);
            const auto count = Field<int>(component, 0xa58), capacity = Field<int>(component, 0xa5c);
            const auto slots = Field<std::uintptr_t>(component, 0xa50);
            const auto fence = Field<std::uintptr_t>(component, 0xa70);
            if (!slots || slots % alignof(void*) || count < 0 || count > 128 || capacity < count
                || ordinal_ >= static_cast<std::size_t>(count)
                || Field<void*>(reinterpret_cast<void*>(slots), ordinal_ * sizeof(void*)) != gpu_target_
                || Field<std::uint8_t>(component, 0xa81)
                || (fence && !(Field<std::uint64_t>(reinterpret_cast<void*>(fence), 8) & (1ull << 26))))
                return Status::failure(FailureCode::GenerationMismatch);
            // A failed undo can already have restored some root pointers.
            // Keep it retryable under the independent component/slot lease;
            // the installed-A pointer graph no longer describes that state.
            gpu_undo_started_ = true;
            if (!Copy(gpu_target_, gpu_undo_.data(), gpu_undo_.size())
                || std::memcmp(gpu_target_, gpu_undo_.data(), gpu_undo_.size()))
                return Status::failure(FailureCode::UndoFailed);
            RebindRootSlots(gpu_target_, object_);
            gpu_target_ = nullptr;
            gpu_write_complete_ = false;
            gpu_undo_started_ = false;
            published_ = false;
            return Status::success();
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
    }
    const bool destroyed=native_destroyed();
    auto* slot = destroyed && execution_settled_ && allocations_.empty()?ResolveDestroyedSlot():ResolveSlot();
    if (!slot) return Status::failure(FailureCode::GenerationMismatch);
    __try
    {
        auto* expected=destroyed?nullptr:object_;
        if (InterlockedCompareExchangePointer(slot, displaced_, expected) != expected)
            return Status::failure(FailureCode::GenerationMismatch);
        published_ = false;
        displaced_ = nullptr;
        ForgetExecutionOwner();
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::Prepared::CommitPublication(void*& displaced) noexcept
{
    displaced = nullptr;
    // B's detached GPU backing and its render transaction need coordinated
    // retirement. The CPU slot-transfer API cannot commit this storage swap.
    if (gpu_storage()) {
        if (!component_replaced_ || !executing_ || !execution_settled_ || displaced_)
            return Status::failure(FailureCode::UnsupportedContent);
        if (!native_destroyed() && (!fresh_gpu_completion_ || !fresh_gpu_completion_->retired()
            || fresh_gpu_completion_->result() != ReplayGpuCompletion::Result::Complete
            || fresh_gpu_completion_->submitted_serial() <= native_death_submission_))
            return Status::failure(FailureCode::IllegalTransition);
        const auto retired = ValidatePublished(); if (!retired.ok()) return retired;
        // Live C arrays stay native-owned; dead C arrays were freed by native
        // teardown. The first record is our separate private header in both cases.
        allocations_.resize(1);
        ForgetExecutionOwner(); published_ = false; gpu_target_ = nullptr;
        Clear();
        return Status::success();
    }
    const auto status = ValidatePublished();
    if (!status.ok()) return status;
    displaced = displaced_;
    ForgetExecutionOwner();
    displaced_ = nullptr; published_ = false; object_ = nullptr;
    allocations_.clear(); // The complete replacement graph now belongs to native code.
    executing_=execution_settled_=false;execution_budget_=0;
    return Status::success();
}

Status Sc6ReplayCpuEmitterState::Prepared::BeginExecution(const Sc6ReplayCpuEmitterState* original,
    std::size_t retirement_budget, const ReplayGpuCompletion* completion) noexcept
{
    if(executing_ || allocations_.empty())return Status::failure(FailureCode::IllegalTransition);
    if(!original && (displaced_ || (gpu_storage() && (!component_replaced_ || !gpu_target_))))return Status::failure(FailureCode::GenerationMismatch);
    if (component_replaced_ && (original || !completion || !completion->retired()))
        return Status::failure(FailureCode::GenerationMismatch);
    auto status=original?original->ValidateDisplaced(*this):ValidatePublished();if(!status.ok())return status;
    if(retirement_budget<owned_bytes() || retirement_budget>SIZE_MAX-owned_bytes())
        return Status::failure(FailureCode::CapacityExceeded);
    // The GPU header is private staging; its live root is gpu_target_. CPU's
    // object itself is handed to native execution. B remains displaced and
    // owned by the enclosing original image/transaction in both cases.
    allocations_.resize(1);
    if(!gpu_storage())allocations_[0].charged=0;
    executing_=true;execution_budget_=retirement_budget;
    if (component_replaced_) {
        fresh_gpu_completion_ = completion;
        native_death_submission_ = completion->submitted_serial();
    }
    if(!gpu_storage() || component_replaced_) {
        const auto* component=reinterpret_cast<void*>(bindings_[1].address);
        execution_slots_=Field<std::uintptr_t>(component,0xa50);
        execution_count_=Field<int>(component,0xa58);execution_capacity_=Field<int>(component,0xa5c);
        AcquireSRWLockExclusive(&emitter_lifetime_lock);
        lifetime_next_=lifetime_head_;lifetime_head_=this;lifetime_registered_=true;
        ReleaseSRWLockExclusive(&emitter_lifetime_lock);
    }
    return Status::success();
}

Status Sc6ReplayCpuEmitterState::Prepared::SettleExecution(const Sc6ReplayCpuEmitterState& observed,
    const Sc6ReplayCpuEmitterState* original, ValidationFailure* failure) noexcept
{
    if(failure){*failure={};failure->phase="admission";}
    if(!executing_ || execution_settled_ || !published_ || observed.base_!=base_
        || observed.kind_!=kind_ || (original && (original->base_!=base_ || original->kind_!=kind_))
        || (!original && (displaced_ || (gpu_storage() && (!component_replaced_ || !gpu_target_)))))
        return Status::failure(FailureCode::IllegalTransition);
    if(failure)failure->phase="live_slot";
    auto* slot=ResolveSlot(true);if(!slot)return Status::failure(FailureCode::GenerationMismatch);
    auto* live=gpu_target_?gpu_target_:object_;
    void* actual{};
    if(!Copy(&actual,const_cast<void**>(slot),sizeof(actual)) || actual!=live)
        return Status::failure(FailureCode::GenerationMismatch);
    if(failure)failure->phase="observed_graph";
    auto status=observed.ValidateNativeGraph(live,failure);if(!status.ok())return status;
    if(original) {
        if(failure)failure->phase="original_graph";
        status=original->ValidateNativeGraph(gpu_target_?static_cast<const void*>(gpu_undo_.data()):displaced_,failure);
        if(!status.ok())return status;
    }
    if(failure)failure->phase="captured_bindings";
    for(std::size_t i=0;i<bindings_.size();++i)
        if(observed.bindings_[i].address!=bindings_[i].address || observed.bindings_[i].weak!=bindings_[i].weak)
            return Status::failure(FailureCode::GenerationMismatch);
    try {
        const auto count=observed.buffers_.size()+1;
        if(count>execution_budget_/sizeof(Allocation))return Status::failure(FailureCode::CapacityExceeded);
        std::vector<Allocation> current;current.reserve(count);
        std::size_t charged=current.capacity()*sizeof(Allocation);
        if(charged>execution_budget_)return Status::failure(FailureCode::CapacityExceeded);
        const auto append=[&](void* address,std::size_t bytes,void* pointer_slot) {
            if(bool(address)!=bool(bytes))return false;
            const auto size=bytes?reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0)(bytes,0):0;
            if(size<bytes || size>execution_budget_-charged)return false;
            charged+=size;current.push_back({address,size,bytes,pointer_slot});return true;
        };
        if(!append(object_,observed.object_bytes(),nullptr))return Status::failure(FailureCode::CapacityExceeded);
        for(std::size_t i=0;i<observed.buffers_.size();++i) {
            const auto& buffer=observed.buffers_[i];
            const auto* header=buffer.parent<0?observed.object_.data():observed.buffers_[buffer.parent].bytes.data();
            auto* address=Field<void*>(header,buffer.offset);
            auto* parent=buffer.parent<0?live:current[std::size_t(buffer.parent)+1].address;
            if(!append(address,buffer.bytes.size(),static_cast<std::byte*>(parent)+buffer.offset))
                return Status::failure(FailureCode::CapacityExceeded);
        }
        const auto overlap=[](const void* first,std::size_t n,const void* second,std::size_t m) {
            const auto a=reinterpret_cast<std::uintptr_t>(first),b=reinterpret_cast<std::uintptr_t>(second);
            return n && m && (n>UINTPTR_MAX-a || m>UINTPTR_MAX-b || (a<b+m && b<a+n));
        };
        for(std::size_t i=0;i<current.size();++i) {
            const auto& a=current[i];
            if(!gpu_storage() && original && overlap(a.address,a.requested,displaced_,original->object_bytes()))
                return Status::failure(FailureCode::RestorePreflightFailed);
            if(gpu_target_ && i && overlap(a.address,a.requested,gpu_target_,observed.object_bytes()))
                return Status::failure(FailureCode::RestorePreflightFailed);
            for(std::size_t j=0;j<i;++j)
                if(overlap(a.address,a.requested,current[j].address,current[j].requested))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            if(original)for(const auto& b:original->buffers_) {
                const auto* header=b.parent<0?original->object_.data():original->buffers_[b.parent].bytes.data();
                if(overlap(a.address,a.requested,Field<void*>(header,b.offset),b.bytes.size()))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            }
        }
        // Preserve GPU C's current root for its existing in-place undo path.
        // B's header and allocations remain untouched. A failed staging write
        // is still unsettled and therefore cannot authorize retirement.
        if(gpu_target_ && !Copy(object_,observed.object_.data(),observed.object_bytes()))
            return Status::failure(FailureCode::RestoreWriteFailed);
        allocations_.swap(current);execution_budget_-=charged;
        published_fingerprint_=BackingFingerprint();execution_settled_=true;
        if(failure)failure->phase="settled_displaced";
        return original?original->ValidateDisplaced(*this):ValidatePublished();
    } catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
}
Status Sc6ReplayCpuEmitterState::Prepared::ReopenExecutionForUndo(const Sc6ReplayCpuEmitterState* original) noexcept
{
    if(native_destroyed()) {
        if (gpu_storage()) {
            if (original || !fresh_gpu_retired_ || !FreshGpuDestructionWitness())
                return Status::failure(FailureCode::GenerationMismatch);
            execution_settled_ = false;
            return Status::success();
        }
        if(FreshComponentDestructionWitness(bindings_[1].address,bindings_[1].weak)) {
            if(original || displaced_ || !fresh_gpu_retired_ || !allocations_.empty())return Status::failure(FailureCode::GenerationMismatch);
            execution_settled_=false;return Status::success();
        }
        if(!ResolveDestroyedSlot())return Status::failure(FailureCode::GenerationMismatch);
        if(!execution_settled_)return Status::success();
        const auto status=original?original->ValidateDisplaced(*this):ValidatePublished();
        if(status.ok())execution_settled_=false;
        return status;
    }
    if(!executing_ || !published_ || gpu_undo_started_ || allocations_.empty())
        return Status::failure(FailureCode::IllegalTransition);
    if(!execution_settled_)return Status::success();
    auto status=original?original->ValidateDisplaced(*this):ValidatePublished();if(!status.ok())return status;
    std::size_t released=allocations_.capacity()*sizeof(Allocation);
    for(std::size_t i=gpu_storage()?1:0;i<allocations_.size();++i) {
        if(allocations_[i].charged>SIZE_MAX-released)return Status::failure(FailureCode::CapacityExceeded);
        released+=allocations_[i].charged;
    }
    if(released>SIZE_MAX-execution_budget_)return Status::failure(FailureCode::CapacityExceeded);
    execution_budget_+=released;
    // Native C keeps its allocation graph. The private GPU header and the
    // displaced B graph remain owned; only C retirement records are removed.
    allocations_.resize(1);if(!gpu_storage())allocations_[0].charged=0;
    execution_settled_=false;return Status::success();
}

bool Sc6ReplayCpuEmitterState::StorageDisjoint(const void* root,
    const Sc6ReplayCpuEmitterState& other, const void* other_root, bool shared_gpu_root) const noexcept
{
    if (!valid_ || !other.valid_ || !root || !other_root) return false;
    const auto region = [](const Sc6ReplayCpuEmitterState& image, const void* root,
        std::size_t index, std::uintptr_t& address, std::size_t& bytes) {
        if (!index) { address = reinterpret_cast<std::uintptr_t>(root); bytes = image.object_bytes(); }
        else {
            const auto& buffer = image.buffers_[index - 1];
            if (buffer.parent < -1 || buffer.parent >= static_cast<int>(index - 1)) return false;
            const auto* data = buffer.parent < 0 ? image.object_.data() : image.buffers_[buffer.parent].bytes.data();
            const auto size = buffer.parent < 0 ? image.object_bytes() : image.buffers_[buffer.parent].bytes.size();
            if (buffer.offset > size || sizeof(void*) > size - buffer.offset) return false;
            address = Field<std::uintptr_t>(data, buffer.offset); bytes = buffer.bytes.size();
        }
        return bool(address) == bool(bytes) && bytes <= UINTPTR_MAX - address;
    };
    for (std::size_t i = 0; i <= buffers_.size(); ++i) {
        std::uintptr_t a{}; std::size_t n{};
        if (!region(*this, root, i, a, n)) return false;
        for (std::size_t j = 0; j <= other.buffers_.size(); ++j) {
            std::uintptr_t b{}; std::size_t m{};
            if (!region(other, other_root, j, b, m)) return false;
            if (!i && !j && shared_gpu_root && gpu_storage() && other.gpu_storage() && a == b && n == m) continue;
            if (n && m && a < b + m && b < a + n) return false;
        }
    }
    return true;
}

Status Sc6ReplayCpuEmitterState::ValidateNativeGraph(const void* emitter, ValidationFailure* failure) const noexcept
{
    const auto bindings = ValidateBindings(failure);
    if (!bindings.ok()) return bindings;
    __try
    {
        if (!emitter || std::memcmp(emitter, object_.data(), object_bytes())) {
            if(failure) {
                failure->check="root_bytes";
                if(emitter)for(std::size_t i=0;i<object_bytes();++i) {
                    const auto expected=std::to_integer<unsigned>(object_[i]);
                    const auto actual=static_cast<const unsigned char*>(emitter)[i];
                    if(expected!=actual){failure->offset=i;failure->expected=expected;failure->actual=actual;break;}
                }
            }
            return Status::failure(FailureCode::GenerationMismatch);
        }
        for (std::size_t i = 0; i < buffers_.size(); ++i)
        {
            const auto& buffer = buffers_[i];
            if (buffer.parent >= static_cast<int>(i)) return Status::failure(FailureCode::RestorePreflightFailed);
            const auto* parent = buffer.parent < 0 ? object_.data() : buffers_[buffer.parent].bytes.data();
            const auto address = Field<std::uintptr_t>(parent, buffer.offset);
            if (!buffer.bytes.empty() && (!address || buffer.bytes.size() > UINTPTR_MAX - address
                || std::memcmp(reinterpret_cast<void*>(address), buffer.bytes.data(), buffer.bytes.size()))) {
                if(failure) {
                    failure->check="buffer_bytes";failure->offset=i;failure->expected=buffer.bytes.size();failure->actual=address;
                    if(address && buffer.bytes.size()<=UINTPTR_MAX-address)for(std::size_t j=0;j<buffer.bytes.size();++j) {
                        const auto expected=std::to_integer<unsigned>(buffer.bytes[j]);
                        const auto actual=reinterpret_cast<const unsigned char*>(address)[j];
                        if(expected!=actual){failure->offset=(i<<32)|j;failure->expected=expected;failure->actual=actual;break;}
                    }
                }
                return Status::failure(FailureCode::GenerationMismatch);
            }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::ValidateDisplaced(const Prepared& replacement) const noexcept
{
    if (!valid_ || kind_ != replacement.kind_ || !replacement.published_
        || !(gpu_storage() ? replacement.gpu_target_ : replacement.displaced_)
        || replacement.gpu_undo_started_ || base_ != replacement.base_
        || thread_ != GetCurrentThreadId()) return Status::failure(FailureCode::IllegalTransition);
    const auto installed = replacement.ValidatePublished();
    if (!installed.ok()) return installed;
    const auto bindings = ValidateBindings();
    if (!bindings.ok()) return bindings;
    __try
    {
        const auto* old_header = gpu_storage() ? replacement.gpu_undo_.data()
            : static_cast<const std::byte*>(replacement.displaced_);
        if (std::memcmp(object_.data(), old_header, object_bytes()))
            return Status::failure(FailureCode::GenerationMismatch);
        const auto original_address = [&](std::size_t i) {
            const auto& buffer = buffers_[i];
            const auto* parent = buffer.parent < 0 ? object_.data() : buffers_[buffer.parent].bytes.data();
            return Field<std::uintptr_t>(parent, buffer.offset);
        };
        for (std::size_t i = 0; i < buffers_.size(); ++i)
        {
            const auto& buffer = buffers_[i];
            if (buffer.parent >= static_cast<int>(i)) return Status::failure(FailureCode::RestorePreflightFailed);
            const auto address = original_address(i), size = buffer.bytes.size();
            if (bool(address) != bool(size) || size > UINTPTR_MAX - address)
                return Status::failure(FailureCode::RestorePreflightFailed);
            if (!size) continue;
            if (std::memcmp(reinterpret_cast<void*>(address), buffer.bytes.data(), size))
                return Status::failure(FailureCode::RestoreVerificationFailed);
            for (std::size_t j = 0; j < i; ++j)
            {
                const auto other = original_address(j), extent = buffers_[j].bytes.size();
                if (extent && (extent > UINTPTR_MAX - other || (address < other + extent && other < address + size)))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            }
            const auto target = reinterpret_cast<std::uintptr_t>(gpu_storage() ? replacement.gpu_target_ : replacement.object_);
            const auto prior = reinterpret_cast<std::uintptr_t>(gpu_storage() ? replacement.gpu_target_ : replacement.displaced_);
            if (object_bytes() > UINTPTR_MAX - target || object_bytes() > UINTPTR_MAX - prior
                || (address < target + object_bytes() && target < address + size)
                || (address < prior + object_bytes() && prior < address + size))
                return Status::failure(FailureCode::RestorePreflightFailed);
            for (const auto& allocation : replacement.allocations_)
            {
                const auto other = reinterpret_cast<std::uintptr_t>(allocation.address);
                if (allocation.requested && (allocation.requested > UINTPTR_MAX - other
                    || (address < other + allocation.requested && other < address + size)))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayCpuEmitterState::CommitDisplacedGpu(Prepared& replacement) const noexcept
{
    if (!gpu_storage()) return Status::failure(FailureCode::IllegalTransition);
    const auto status = ValidateDisplaced(replacement);
    if (!status.ok()) return status;
    // Attached callback objects were excluded by CaptureGpu. These are
    // independent native allocations, including scalar payload and burst rows.
    // Release children first while their immutable captured parent images
    // remain available. Never invoke the GPU destructor on the live emitter.
    for (std::size_t i = buffers_.size(); i > 0; --i)
    {
        const auto& buffer = buffers_[i - 1];
        const auto* parent = buffer.parent < 0 ? object_.data() : buffers_[buffer.parent].bytes.data();
        if (auto* address = Field<void*>(parent, buffer.offset))
            reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(address);
    }
    // The prepared object was only a header staging allocation. Its backing
    // now belongs to the native target; discard metadata without freeing it.
    reinterpret_cast<void (*)(void*)>(base_ + 0xd46a00)(replacement.object_);
    replacement.object_ = replacement.gpu_target_ = nullptr;
    replacement.published_ = replacement.gpu_write_complete_ = false;
    replacement.allocations_.clear();
    replacement.executing_=replacement.execution_settled_=false;replacement.execution_budget_=0;
    return Status::success();
}

Status Sc6ReplayCpuEmitterState::PrepareReplacement(std::size_t budget, Prepared& output,
    const ComponentReplacement& component) const noexcept
{
    return PrepareReplacementImpl(budget,output,&component);
}

Status Sc6ReplayCpuEmitterState::Prepared::ValidateFreshGpuDestination(
    std::size_t ordinal,void* constructed) noexcept
{
    if(!component_replaced_ || !gpu_storage() || published_ || !object_
        || !constructed || constructed==object_ || thread_!=GetCurrentThreadId())
        return Status::failure(FailureCode::IllegalTransition);
    ordinal_=ordinal;
    auto* slot=ResolveSlot();
    if(!slot)return Status::failure(FailureCode::GenerationMismatch);
    __try {
        if(*slot!=constructed || Field<std::uintptr_t>(constructed,0)!=base_+Vtable(Kind::Gpu)
            || Field<std::uintptr_t>(constructed,0x10) || Field<std::uintptr_t>(constructed,0x18)
            || Field<std::uintptr_t>(constructed,0x28))
            return Status::failure(FailureCode::RestorePreflightFailed);
        // These fields are zeroed by the native base/GPU constructors. Never
        // turn an initialized old root into a fresh owner by clearing them.
        if(!EmptyGpuConstructorStorage(constructed))return Status::failure(FailureCode::RestorePreflightFailed);
        for(const auto offset:{0x1d0,0x1d8})
            if(!Field<void*>(constructed,offset) || Field<void*>(constructed,offset)!=Field<void*>(object_,offset))
                return Status::failure(FailureCode::GenerationMismatch);
        const auto* render=Field<void*>(constructed,0x1e0);
        // A retired allocation address may be returned by the native factory
        // again. Preserve rejection when the original weak owner is still live
        // (or no original identity was retained), without reading its storage.
        const bool captured_alias=render==Field<void*>(object_,0x1e0);
        if(!render || (captured_alias && (!replaced_source_.address
                || reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(replaced_source_.weak.data())))
            || Field<std::uintptr_t>(render,0)!=base_+0x394bfc0
            || Field<std::uintptr_t>(render,0x1f0)!=base_+0x394c000
            || Field<int>(render,0x240)!=-1 || Field<std::uint8_t>(render,0x28)
            || Field<std::uint8_t>(render,0x218) || Field<std::uint8_t>(render,0x252))
            return Status::failure(FailureCode::RestorePreflightFailed);
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER){return Status::failure(FailureCode::RestorePreflightFailed);}
}

Status Sc6ReplayCpuEmitterState::Prepared::PublishFreshGpuStorage(
    std::size_t ordinal,void* constructed,bool& dirty) noexcept
{
    if (dirty) return Status::failure(FailureCode::IllegalTransition);
    const auto ready = ValidateFreshGpuDestination(ordinal, constructed);
    if (!ready.ok()) return ready;
    __try {
        dirty=true;
        std::memcpy(static_cast<std::byte*>(constructed)+0x18,&bindings_[1].address,sizeof(std::uintptr_t));
        return PublishGpuStorage(ordinal,constructed);
    } __except(EXCEPTION_EXECUTE_HANDLER){return Status::failure(FailureCode::RestoreWriteFailed);}
}

Status Sc6ReplayCpuEmitterState::ConstructFreshGpuOwner(const ComponentReplacement& component,
    std::size_t budget,FreshGpuOwner& output) const noexcept
{
    if(output.phase!=FreshGpuOwner::Phase::Empty || output.root || !gpu_storage())
        return Status::failure(FailureCode::IllegalTransition);
    const auto ready=ValidateBindingsForComponent(&component);
    if(!ready.ok())return ready;
    __try {
        const auto system=Field<std::uintptr_t>(object_.data(),0x1d0);
        const auto descriptor=Field<std::uintptr_t>(object_.data(),0x1d8);
        if(!system || !descriptor || Field<std::uintptr_t>(reinterpret_cast<void*>(component.target),0xa60)!=system)
            return Status::failure(FailureCode::GenerationMismatch);
        if(const auto field=bindings_[8].address) {
            const auto table=Field<std::uintptr_t>(reinterpret_cast<void*>(field),0);
            // The static field constructor borrows asset+58. Animated/unknown
            // overrides may allocate more or submit work outside this journal.
            if(table!=base_+0x39e5068 || Field<std::uintptr_t>(reinterpret_cast<void*>(table),0x230)!=base_+0x21a31f0
                || !Field<std::uintptr_t>(reinterpret_cast<void*>(field),0x58))
                return Status::failure(FailureCode::UnsupportedContent);
        }
        const auto charge=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base_+0xd50dc0);
        const auto root_bytes=charge(0x2a0,0),render_bytes=charge(0x260,0);
        if(root_bytes<0x2a0 || render_bytes<0x260 || root_bytes>budget
            || render_bytes>budget-root_bytes || sizeof(FreshGpuOwner)>budget-root_bytes-render_bytes)
            return Status::failure(FailureCode::CapacityExceeded);
        output.component=component.target;output.system=system;output.descriptor=descriptor;
        output.charged_bytes=root_bytes+render_bytes+sizeof(FreshGpuOwner);
        output.phase=FreshGpuOwner::Phase::Constructing;
        output.root=reinterpret_cast<void*(*)(void*,void*)>(base_+0x1f941e0)
            (reinterpret_cast<void*>(system),reinterpret_cast<void*>(descriptor));
        if(!output.root) {output.phase=FreshGpuOwner::Phase::Failed;return Status::failure(FailureCode::CaptureFailed);}
        output.render=Field<std::uintptr_t>(output.root,0x1e0);
        output.captured_render=Field<std::uintptr_t>(object_.data(),0x1e0);
        // Native construction owns a new allocation; equality with an expired
        // A address is allocator reuse, not aliasing complete B. The original
        // indexed weak identity still excludes a live historical owner.
        unsigned failed{};
        if(Field<std::uintptr_t>(output.root,0)!=base_+Vtable(Kind::Gpu))failed|=1;
        if(Field<std::uintptr_t>(output.root,0x1d0)!=system)failed|=2;
        if(Field<std::uintptr_t>(output.root,0x1d8)!=descriptor)failed|=4;
        if(!output.render)failed|=8;
        if(output.render==output.captured_render
            && reinterpret_cast<std::uintptr_t(*)(const void*)>(base_+0xf823f0)(component.source_weak.data()))failed|=16;
        if(output.render && Field<std::uintptr_t>(reinterpret_cast<void*>(output.render),0)!=base_+0x394bfc0)failed|=32;
        if(output.render && Field<int>(reinterpret_cast<void*>(output.render),0x240)!=-1)failed|=64;
        if(!ValidateBindingsForComponent(&component).ok())failed|=128;
        output.verification_failure=failed;
        if(failed) {
            output.phase=FreshGpuOwner::Phase::Failed;
            return Status::failure(FailureCode::RestoreVerificationFailed);
        }
        output.phase=FreshGpuOwner::Phase::Constructed;
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(output.phase!=FreshGpuOwner::Phase::Empty)output.phase=FreshGpuOwner::Phase::Failed;
        return Status::failure(FailureCode::ContextUnavailable);
    }
}

Status Sc6ReplayCpuEmitterState::PrepareReplacement(std::size_t budget, Prepared& output) const noexcept
{
    return PrepareReplacementImpl(budget,output,nullptr);
}

Status Sc6ReplayCpuEmitterState::QueueFreshGpuRetirement(
    const ComponentReplacement& component,FreshGpuOwner& owner) const noexcept
{
    if(owner.phase!=FreshGpuOwner::Phase::Constructed || !owner.root || owner.component!=component.target)
        return Status::failure(FailureCode::IllegalTransition);
    const auto ready=ValidateBindingsForComponent(&component);
    if(!ready.ok())return ready;
    __try {
        const auto* root=owner.root;
        const auto* render=reinterpret_cast<void*>(owner.render);
        const auto* c=reinterpret_cast<void*>(component.target);
        const auto count=Field<int>(c,0xa58),capacity=Field<int>(c,0xa5c);
        const auto* slots=Field<void* const*>(c,0xa50);
        if(count<0 || capacity<count || capacity>128 || (capacity && !slots)
            || Field<std::uintptr_t>(c,0xa70) || Field<unsigned char>(c,0xa80) || Field<unsigned char>(c,0xa81))
            return Status::failure(FailureCode::RestorePreflightFailed);
        for(int i=0;i<count;++i)if(slots[i]==root)return Status::failure(FailureCode::RestorePreflightFailed);
        if(Field<std::uintptr_t>(root,0)!=base_+Vtable(Kind::Gpu)
            || (Field<std::uintptr_t>(root,0x18) && Field<std::uintptr_t>(root,0x18)!=component.target)
            || Field<std::uintptr_t>(root,0x1d0)!=owner.system
            || owner.system!=Field<std::uintptr_t>(object_.data(),0x1d0)
            || Field<std::uintptr_t>(root,0x1d8)!=owner.descriptor
            || owner.descriptor!=Field<std::uintptr_t>(object_.data(),0x1d8)
            || Field<std::uintptr_t>(root,0x1e0)!=owner.render || !EmptyGpuConstructorStorage(root)
            || !render || Field<std::uintptr_t>(render,0)!=base_+0x394bfc0
            || Field<std::uintptr_t>(render,0x1f0)!=base_+0x394c000
            || Field<int>(render,0x240)!=-1 || Field<unsigned char>(render,0x28)
            || Field<unsigned char>(render,0x218) || Field<unsigned char>(render,0x252)
            || Field<int>(render,0xc8) || Field<int>(render,0xd8))
            return Status::failure(FailureCode::RestorePreflightFailed);
        owner.phase=FreshGpuOwner::Phase::Destroying;
        reinterpret_cast<void*(*)(void*,unsigned)>(base_+0x1f90680)(owner.root,1);
        // root is freed; render may already be freed or queued. Neither may
        // be inspected again. The token/charge survives through external drain.
        owner.phase=FreshGpuOwner::Phase::RetirementQueued;
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(owner.phase==FreshGpuOwner::Phase::Destroying)owner.phase=FreshGpuOwner::Phase::Failed;
        return Status::failure(FailureCode::ContextUnavailable);
    }
}

Status Sc6ReplayCpuEmitterState::PrepareReplacementImpl(std::size_t budget, Prepared& output,
    const ComponentReplacement* component) const noexcept
{
    if (output.object_ || !output.allocations_.empty()) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateBindingsForComponent(component);
    if (!status.ok()) return status;
    if (owned_bytes() > budget) return Status::failure(FailureCode::CapacityExceeded);
    const auto remaining = budget - owned_bytes();
    try
    {
        output.base_ = base_;
        output.kind_ = kind_;
        output.bindings_ = bindings_;
        output.component_replaced_ = component!=nullptr;
        if(component) {
            output.replaced_source_=bindings_[1];
            output.bindings_[1]={component->target,component->target_weak};
        }
        output.thread_ = thread_;
        output.lod_peak_ = lod_peak_;
        if (remaining < sizeof(Prepared) || buffers_.size() + 1 > (remaining - sizeof(Prepared)) / sizeof(Prepared::Allocation))
            return Status::failure(FailureCode::CapacityExceeded);
        output.allocations_.reserve(buffers_.size() + 1);
        output.object_ = output.Allocate(object_bytes(), remaining);
        if (!output.object_) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        std::memcpy(output.object_, object_.data(), object_bytes());
        if(component)std::memcpy(static_cast<std::byte*>(output.object_)+0x18,&component->target,sizeof(component->target));
        for (const auto& buffer : buffers_)
        {
            auto* memory = output.Allocate(buffer.bytes.size(), remaining);
            if (!buffer.bytes.empty() && !memory) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
            if (memory) std::memcpy(memory, buffer.bytes.data(), buffer.bytes.size());
            auto* parent = buffer.parent < 0 ? output.object_ : output.allocations_[static_cast<std::size_t>(buffer.parent) + 1].address;
            auto* slot = static_cast<std::byte*>(parent) + buffer.offset;
            std::memcpy(slot, &memory, sizeof(memory));
            output.allocations_.back().pointer_slot = slot;
        }
        status = ValidateBindingsForComponent(component);
        if (!status.ok()) output.Clear();
        return status;
    }
    catch (...) { output.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
}
}
