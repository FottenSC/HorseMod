#include "MotionBankSnapshot.hpp"
#include "LocalImageChecksum.hpp"
#include "CharaAnimationState.hpp"

#include <bit>
#include <algorithm>
#include <cstring>

namespace Horse::Deterministic
{
namespace
{
constexpr std::array<std::ptrdiff_t, 2> bank_offsets{0x35A0, 0x27760};
constexpr std::array<std::size_t, 2> bank_sizes{
    motion_bank_primary_bytes, motion_bank_secondary_bytes};

template <typename T>
bool read_value(INativeMemory& memory, std::uintptr_t address, T& output) noexcept
{
    return memory.Read(address, std::as_writable_bytes(std::span{&output, 1}));
}

template <typename T>
bool write_value(INativeMemory& memory, std::uintptr_t address,
    const T& value) noexcept
{
    return memory.Write(address, std::as_bytes(std::span{&value, 1}));
}
}

MotionBankSnapshot::MotionBankSnapshot(INativeMemory& memory) noexcept
    : memory_(memory)
{
}

void MotionBankSnapshot::Invalidate() noexcept
{
    animation_owner_ = nullptr;
    animation_binding_serial_ = 0;
    fighters_ = {};
    matrix_counts_ = {};
    topology_ = {};
    context_ = {};
    bound_ = false;
    regions_.clear(); guards_.clear(); colliders_.clear(); collision_nodes_.clear();
    binding_fingerprint_=0; image_bytes_ = motion_bank_base_image_bytes;
}

void MotionBankSnapshot::ReleaseScratchStorage() noexcept
{
    Invalidate();
    undo_scratch_ = {};
    observed_scratch_ = {};
    regions_ = {}; guards_ = {}; colliders_ = {}; collision_nodes_ = {};
}

Status MotionBankSnapshot::Bind(
    const std::array<std::uintptr_t, 2>& fighters,
    const LocalReconstructionGenerationContext& context,
    CharaAnimationState* animation_owner) noexcept
{
    Invalidate();
    if (fighters[0] == 0 || fighters[1] == 0
        || context.round_generation == 0)
        return Status::failure(FailureCode::InvalidConfiguration);
    fighters_ = fighters;
    context_ = context;
    animation_owner_ = animation_owner;
    if (animation_owner_ && (animation_owner_->fighters() != fighters_
        || !animation_owner_->ValidateMotionRuntimeBindings()))
        return Status::failure(FailureCode::IdentityMismatch);
    if (animation_owner_) animation_binding_serial_ = animation_owner_->local_binding_serial();
    for (std::size_t player = 0; player < 2; ++player)
    {
        std::int32_t matrix_count{};
        if (!read_value(memory_, fighters[player] + 0x42550, matrix_count)
            || matrix_count <= 0 || matrix_count > 768)
            return Status::failure(FailureCode::AdapterUnqualified);
        matrix_counts_[player] = matrix_count;
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            auto& topology = topology_[player][bank];
            topology.bank = fighters[player] + bank_offsets[bank];
            topology.bytes = bank_sizes[bank];
            if (!read_value(memory_, topology.bank, topology.vtable)
                || topology.vtable == 0)
                return Status::failure(FailureCode::IdentityMismatch);
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!read_value(memory_, topology.bank + 8 + slot * 8,
                        topology.buffers[slot])
                    || topology.buffers[slot] == 0)
                    return Status::failure(FailureCode::IdentityMismatch);
                for (std::size_t prior = 0; prior < slot; ++prior)
                    if (topology.buffers[prior] == topology.buffers[slot])
                        return Status::failure(FailureCode::IdentityMismatch);
            }
        }
    }
    if (!bind_skeleton_values())
        return Status::failure(FailureCode::AdapterUnqualified);
    try
    {
        // Restore is a live simulation-path transaction. Prepare both the
        // undo image and the verification image while binding so the first
        // correction cannot allocate after qualification has begun.
        undo_scratch_.bytes.reserve(image_bytes_);
        observed_scratch_.bytes.reserve(image_bytes_);
    }
    catch (...)
    {
        Invalidate();
        return Status::failure(FailureCode::CapacityExceeded);
    }
    bound_ = true;
    return topology_matches()
        ? Status::success() : Status::failure(FailureCode::IdentityMismatch);
}

std::size_t MotionBankSnapshot::BindAllocationEnvelopeBytes() noexcept
{
    // The existing image limit admits at most 0xA0000/36 complete nodes:
    // every node emits the 16+4+16 byte prefix. One rejected partial node
    // may already have appended its guards. At most 22 guards belong to a
    // node, 3 to each of 4*768 chains, and 24 fixed guards to each fighter.
    // Every value region consumes at least two image bytes. Include old/new
    // vector coexistence and both local cycle-detection lists, without
    // allocating these deliberately conservative worst-case capacities.
    constexpr std::size_t nodes = 0xA0000 / 36 + 1;
    constexpr std::size_t guards = 2 * 24 + 4 * 768 * 3 + nodes * 22;
    constexpr std::size_t regions = 0xA0000 / 2 + 1;
    return 3 * (guards * sizeof(BindingGuard) + regions * sizeof(ValueRegion)
        + (nodes + 768) * sizeof(std::uintptr_t)
        + nodes * (4 * sizeof(ColliderBinding) + sizeof(std::uintptr_t)))
        + 2 * motion_bank_image_bytes;
}

// Native 140312040 constructs auxiliary nodes in fighter+[45700,95700),
// through 14034A100. The lists, vtables and borrowed constraint links are
// binding data. Only the verified pointer-free solver/transform fields below
// enter this local image. No expected observation is used for reconstruction.
bool MotionBankSnapshot::bind_skeleton_values() noexcept
{
    binding_diagnostic_={};
    auto reject=[&](unsigned line) { if(!binding_diagnostic_.line) binding_diagnostic_.line=line; return false; };
    try {
        auto guard = [&](std::uintptr_t address, std::size_t bytes, const char* kind="binding") {
            BindingGuard item{address,0,bytes,kind};
            if (!memory_.Read(address,std::as_writable_bytes(std::span{&item.value,1}).first(bytes))) return reject(__LINE__);
            guards_.push_back(item); return true;
        };
        auto values = [&](std::uintptr_t address, std::size_t bytes) {
            if (bytes > motion_bank_image_bytes-image_bytes_) return reject(__LINE__);
            regions_.push_back({address,bytes}); image_bytes_ += bytes; return true;
        };
        for (auto fighter : fighters_) {
            const auto skeleton=fighter+0x29120, begin=fighter+0x45700, end=fighter+0x95700;
            std::array<std::int16_t,4> counts{};
            if (!memory_.Read(skeleton,std::as_writable_bytes(std::span{counts}))) return reject(__LINE__);
            for(auto count:counts) if(count<0 || count>768) return reject(__LINE__);
            if (!guard(skeleton,8)) return reject(__LINE__);
            for (const auto offset : {8u,16u,24u,32u}) if(!guard(skeleton+offset,8)) return reject(__LINE__);
            // 14034B8D0 consumes the retained per-player wind/delta and
            // dispatch mode. Global wind restoration does not restore this
            // previously sampled force. These fields are value-only.
            for(const auto& field : std::array<ValueRegion,5>{{
                {skeleton+0x18c,4},{skeleton+0x1d0,2},{skeleton+0x1e4,4},
                {skeleton+0x1e8,4},{skeleton+0x1f0,16}}})
                if(!values(field.address,field.bytes)) return reject(__LINE__);
            // 140303230 initializes these arrays/slots; 14034C650 consumes
            // and advances the secondary cache and seven masked MOT overlays.
            const auto motion=skeleton+0x200;
            if(!values(motion,0x348) || !values(motion+0x370,0x1868)
                || !values(motion+0x20b0,4)) return reject(__LINE__);
            for(unsigned offset=0x348;offset<0x370;offset+=8)
                if(!guard(motion+offset,8)) return reject(__LINE__);
            for(unsigned slot=0;slot<7;++slot) {
                const auto playback=motion+0x1bd8+slot*0xb0;
                // Slot 5 aliases CharaAnimation's runtime union. Its owner
                // validates every current pointer and restores its identity.
                // Standalone motion snapshots retain the immutable guard.
                const bool delegated = animation_owner_ && slot == 5;
                if(!guard(playback,8) || (!delegated && !guard(playback+8,8))
                    || !values(playback+0x10,0xa0)) return reject(__LINE__);
            }
            // The source follows a rotating native matrix bank, not one
            // immutable buffer address. Capture its dictionary index below.
            auto inside=[&](std::uintptr_t p,std::size_t size){return p>=begin && p<end && size<=end-p;};
            std::vector<std::uintptr_t> seen;
            auto node = [&](std::uintptr_t p,bool spring) {
                binding_diagnostic_.address=p;
                if(!inside(p,spring?0x2b0:0x50)
                    || std::find(seen.begin(),seen.end(),p)!=seen.end()) return reject(__LINE__);
                seen.push_back(p);
                for(const auto offset:{0u,0x20u,0x28u}) if(!guard(p+offset,8)) return reject(__LINE__);
                if(!guard(p+0x34,4,"node_tag")) return reject(__LINE__);
                if(!values(p+0x10,16) || !values(p+0x30,4) || !values(p+0x40,16)) return reject(__LINE__);
                std::uint16_t type{},bone{};
                if(!read_value(memory_,p+0x36,type) || !read_value(memory_,p+0x20,bone)) return reject(__LINE__);
                binding_diagnostic_.observed=type;
                if(!spring) {
                    // Type 3 is Face, except the two native Eye outputs selected
                    // by 14034A100. Constructors 1403896B0/140389060 and their
                    // constraint callees establish these scalar-only suffixes.
                    if(type==3) {
                        const std::size_t bytes=(bone==0x41 || bone==0x4d)?0x34:0xc;
                        if(!inside(p,0x50+bytes) || !values(p+0x50,bytes)) return reject(__LINE__);
                    }
                    return true;
                }
                if(type!=1 && type!=0xb && type!=0x1e) return reject(__LINE__);
                if(!guard(p+0x50,8,"spring_binding") || !guard(p+0xd8,2,"spring_constraints")
                    || !guard(p+0xdc,4)
                    || !values(p+0x58,0x80) || !values(p+0x2a0,16)) return reject(__LINE__);
                std::int16_t constraints{},collisions{};
                if(!read_value(memory_,p+0xd8,constraints) || !read_value(memory_,p+0xda,collisions)
                    || constraints<0 || constraints>4 || collisions<0 || collisions>4) return reject(__LINE__);
                // 14033A990 resolves these pointers once. 140338870 and the
                // spring solvers update their scalar distance/point scratch.
                for(int i=0;i<constraints;++i)
                    if(!guard(p+0xe0+i*16,8) || !values(p+0xe8+i*16,8)) return reject(__LINE__);
                // Native 140336F80 grows +DA and writes a borrowed collider
                // into +170+60*i. All four local point caches are mutable.
                collision_nodes_.push_back(p);
                if(18 > motion_bank_image_bytes-image_bytes_) return reject(__LINE__);
                image_bytes_+=18; // ushort count and four uint dictionary IDs
                for(int i=0;i<4;++i) {
                    if(!values(p+0x120+i*0x60,0x50)) return reject(__LINE__);
                    std::uintptr_t collider{};
                    if(!read_value(memory_,p+0x170+i*0x60,collider)) return reject(__LINE__);
                    if(!collider) { if(i<collisions) return reject(__LINE__); continue; }
                    if(!inside(collider,0xe8)) return reject(__LINE__);
                    if(std::none_of(colliders_.begin(),colliders_.end(),[&](const auto& item){return item.address==collider;})) {
                        ColliderBinding binding{collider};
                        if(!read_value(memory_,collider,binding.vtable) || !binding.vtable
                            || !read_value(memory_,collider+0xe4,binding.runtime_index)
                            || binding.runtime_index>=64) return reject(__LINE__);
                        colliders_.push_back(binding);
                    }
                }
                if(type==1) {
                    if(!inside(p,0x2c0) || !guard(p+0x2b0,8) || !guard(p+0x2b8,8)) return reject(__LINE__);
                } else if(type==0x1e) {
                    if(!inside(p,0x300)) return reject(__LINE__);
                    for(unsigned offset=0x2b0;offset<0x2e0;offset+=8) if(!guard(p+offset,8)) return reject(__LINE__);
                    if(!guard(p+0x2e0,8) || !guard(p+0x2e8,8) || !values(p+0x2f0,12)) return reject(__LINE__);
                }
                return true;
            };
            for(unsigned bank=0;bank<2;++bank) {
                std::uintptr_t p{};
                if(!read_value(memory_,skeleton+8+bank*16,p)) return reject(__LINE__);
                // 14034A100's classification count omits type 0x1D even
                // though its construction branch appends a node. The actual
                // consumer 14034B8D0 walks links to null. Keep every link in the
                // binding proof and use the native 768-record capacity as bound.
                for(unsigned i=0;p;++i) {
                    if(i==768 || !node(p,false) || !read_value(memory_,p+0x28,p)) return reject(__LINE__);
                }
                if(!read_value(memory_,skeleton+16+bank*16,p)) return reject(__LINE__);
                std::vector<std::uintptr_t> chains;
                for(int i=0;i<counts[bank*2+1];++i) {
                    binding_diagnostic_.address=p; binding_diagnostic_.observed=counts[bank*2+1];
                    if(!inside(p,0x60) || std::find(chains.begin(),chains.end(),p)!=chains.end()) return reject(__LINE__);
                    chains.push_back(p);
                    if(!guard(p+0x40,8) || !guard(p+0x48,8) || !guard(p+0x50,2)
                        || !values(p,0x40) || !values(p+0x52,2)) return reject(__LINE__);
                    std::uint8_t count{};std::uintptr_t child{};
                    if(!read_value(memory_,p+0x50,count) || !count || !read_value(memory_,p+0x48,child)) return reject(__LINE__);
                    for(unsigned j=0;j<count;++j)
                        if(!node(child,true) || !read_value(memory_,child+0x28,child)) return reject(__LINE__);
                    if(child || !read_value(memory_,p+0x40,p)) return reject(__LINE__);
                }
                if(p) return reject(__LINE__);
            }
        }
        binding_fingerprint_=1469598103934665603ull;
        for(const auto& item:guards_)
            for(auto value:{std::uint64_t(item.address),item.value,std::uint64_t(item.bytes)})
                binding_fingerprint_=(binding_fingerprint_^value)*1099511628211ull;
        for(const auto& item:colliders_)
            for(auto value:{std::uint64_t(item.address),std::uint64_t(item.vtable),std::uint64_t(item.runtime_index)})
                binding_fingerprint_=(binding_fingerprint_^value)*1099511628211ull;
        for(auto node:collision_nodes_) binding_fingerprint_=(binding_fingerprint_^node)*1099511628211ull;
        return true;
    } catch (...) { return reject(__LINE__); }
}

bool MotionBankSnapshot::identify_secondary_source(std::uintptr_t pointer,std::uint8_t& slot) const noexcept
{
    slot=0;
    if(!pointer) return true;
    for(const auto& player:topology_) for(const auto& bank:player) for(auto buffer:bank.buffers) {
        ++slot;
        if(pointer==buffer) return true;
    }
    return false;
}
std::uintptr_t MotionBankSnapshot::resolve_secondary_source(std::uint8_t slot) const noexcept
{
    if(!slot) return 0;
    for(const auto& player:topology_) for(const auto& bank:player) for(auto buffer:bank.buffers)
        if(--slot==0) return buffer;
    return 0;
}

bool MotionBankSnapshot::collision_values_valid(const LocalReconstructionImage* image) noexcept
{
    if(image && image->bytes.size()!=image_bytes_) return false;
    std::size_t cursor=image_bytes_-collision_nodes_.size()*18;
    for(auto node:collision_nodes_) {
        std::uint16_t count{};
        if(image) std::memcpy(&count,image->bytes.data()+cursor,2);
        else if(!read_value(memory_,node+0xda,count)) return false;
        cursor+=2;
        if(count>4) return false;
        for(unsigned slot=0;slot<4;++slot) {
            std::uintptr_t pointer{};
            if(image) {
                std::uint32_t index{};std::memcpy(&index,image->bytes.data()+cursor,4);
                if(index>colliders_.size()) return false;
                if(index) pointer=colliders_[index-1].address;
            } else if(!read_value(memory_,node+0x170+slot*0x60,pointer)) return false;
            cursor+=4;
            if(!pointer) {if(slot<count) return false;continue;}
            if(std::none_of(colliders_.begin(),colliders_.end(),[&](const auto& item){return item.address==pointer;})) return false;
            bool same_owner=false;
            for(auto fighter:fighters_)
                same_owner|=node>=fighter+0x45700 && node<fighter+0x95700
                    && pointer>=fighter+0x45700 && pointer<fighter+0x95700;
            if(!same_owner) return false;
        }
    }
    return cursor==image_bytes_;
}

bool MotionBankSnapshot::topology_matches(bool check_collision_values) noexcept
{
    if (!bound_) return false;
    if (animation_owner_ && (animation_owner_->fighters() != fighters_
        || animation_owner_->local_binding_serial() != animation_binding_serial_
        || !animation_owner_->ValidateMotionRuntimeBindings())) {
        binding_diagnostic_={__LINE__,0,0,0,0,"animation_runtime_owner"};
        return false;
    }
    for (const auto& guard : guards_) {
        std::uint64_t value{};
        if (!memory_.Read(guard.address, std::as_writable_bytes(std::span{&value,1}).first(guard.bytes))
            || value != guard.value) {
            binding_diagnostic_={__LINE__,guard.address,value,guard.value,0,guard.kind};
            for(auto fighter:fighters_) if(guard.address>=fighter && guard.address-fighter<0x97490)
                binding_diagnostic_.fighter_offset=guard.address-fighter;
            return false;
        }
    }
    for(const auto& collider:colliders_) {
        std::uintptr_t vtable{};std::uint32_t index{};
        if(!read_value(memory_,collider.address,vtable) || vtable!=collider.vtable
            || !read_value(memory_,collider.address+0xe4,index) || index!=collider.runtime_index) return false;
    }
    if(check_collision_values && !collision_values_valid()) return false;
    for (std::size_t player_index = 0;
         player_index < topology_.size(); ++player_index)
    {
        std::uintptr_t secondary{};std::uint8_t secondary_slot{};
        if(!read_value(memory_,fighters_[player_index]+0x2b3c8,secondary)
            || !identify_secondary_source(secondary,secondary_slot)) return false;
        std::int32_t matrix_count{};
        if (!read_value(memory_, fighters_[player_index] + 0x42550,
                matrix_count)
            || matrix_count != matrix_counts_[player_index]) return false;
        for (const auto& expected : topology_[player_index])
        {
            std::uintptr_t vtable{}, current{}, provider{};
            std::uint32_t active{};
            if (!read_value(memory_, expected.bank, vtable)
                || vtable != expected.vtable
                || !read_value(memory_, expected.bank + 0x20, active)
                || active >= 3
                || !read_value(memory_, expected.bank + 0x28, current)
                || !read_value(memory_, expected.bank + 0x30, provider))
                return false;
            bool current_found{}, provider_found{};
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                std::uintptr_t buffer{};
                if (!read_value(memory_, expected.bank + 8 + slot * 8, buffer)
                    || buffer != expected.buffers[slot]) return false;
                current_found = current_found || current == buffer;
                provider_found = provider_found || provider == buffer;
            }
            if (!current_found || !provider_found
                || current != expected.buffers[active]) return false;
        }
    }
    return true;
}

std::uint64_t MotionBankSnapshot::Checksum(
    const LocalReconstructionImage& image) noexcept
{
    LocalImageChecksum checksum;
    checksum.Add(&image.serializer_id, sizeof(image.serializer_id));
    checksum.Add(&image.serializer_version, sizeof(image.serializer_version));
    checksum.Add(&image.context, sizeof(image.context));
    checksum.Add(&image.cursor, sizeof(image.cursor));
    checksum.Add(image.bytes.data(), image.bytes.size());
    return checksum.Finish();
}

bool MotionBankSnapshot::ValidateLocalImageMetadata(
    const LocalReconstructionImage& image) noexcept
{
    return image.serializer_id == LocalSerializerId::MotionBankTriples
        && image.serializer_version == motion_bank_serializer_version
        && image.cursor == image.bytes.size()
        && image.bytes.size() >= motion_bank_base_image_bytes
        && image.bytes.size() <= motion_bank_image_bytes;
}

bool MotionBankSnapshot::ValidateLocalImage(
    const LocalReconstructionImage& image) noexcept
{
    return ValidateLocalImageMetadata(image) && image.checksum == Checksum(image);
}

Status MotionBankSnapshot::capture_unchecked(
    LocalReconstructionImage& output) noexcept
{
    output.serializer_id = LocalSerializerId::MotionBankTriples;
    output.serializer_version = motion_bank_serializer_version;
    output.context = {};
    output.cursor = 0;
    output.checksum = 0;
    if (!topology_matches())
        return Status::failure(FailureCode::IdentityMismatch);
    try { output.bytes.resize(image_bytes_); }
    catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
    output.context = context_;
    output.cursor = output.bytes.size();
    std::size_t cursor = 10;
    for(unsigned player=0;player<2;++player) {
        std::uintptr_t source{};std::uint8_t slot{};
        if(!read_value(memory_,fighters_[player]+0x2b3c8,source) || !identify_secondary_source(source,slot))
            return Status::failure(FailureCode::IdentityMismatch);
        output.bytes[8+player]=std::byte{slot};
    }
    std::size_t metadata{};
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            const auto& topology = topology_[player][bank];
            std::uintptr_t current{}, provider{};
            if (!read_value(memory_, topology.bank + 0x28, current)
                || !read_value(memory_, topology.bank + 0x30, provider))
                return Status::failure(FailureCode::CaptureFailed);
            std::uint8_t current_slot{0xff}, provider_slot{0xff};
            for (std::uint8_t slot = 0; slot < 3; ++slot)
            {
                if (topology.buffers[slot] == current) current_slot = slot;
                if (topology.buffers[slot] == provider) provider_slot = slot;
            }
            if (current_slot >= 3 || provider_slot >= 3)
                return Status::failure(FailureCode::IdentityMismatch);
            output.bytes[metadata++] = std::byte{current_slot};
            output.bytes[metadata++] = std::byte{provider_slot};
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!memory_.Read(topology.buffers[slot],
                        std::span{output.bytes}.subspan(cursor, topology.bytes)))
                    return Status::failure(FailureCode::CaptureFailed);
                cursor += topology.bytes;
            }
        }
        if (!memory_.Read(fighters_[player] + motion_tail_fighter_offset,
                std::span{output.bytes}.subspan(cursor, motion_tail_bytes)))
            return Status::failure(FailureCode::CaptureFailed);
        cursor += motion_tail_bytes;
    }
    std::memcpy(output.bytes.data()+cursor,&binding_fingerprint_,8); cursor+=8;
    for (const auto& region : regions_) {
        if (!memory_.Read(region.address,std::span{output.bytes}.subspan(cursor,region.bytes)))
            return Status::failure(FailureCode::CaptureFailed);
        cursor += region.bytes;
    }
    for(auto node:collision_nodes_) {
        std::uint16_t count{};
        if(!read_value(memory_,node+0xda,count)) return Status::failure(FailureCode::CaptureFailed);
        std::memcpy(output.bytes.data()+cursor,&count,2);cursor+=2;
        for(unsigned slot=0;slot<4;++slot) {
            std::uintptr_t pointer{};
            if(!read_value(memory_,node+0x170+slot*0x60,pointer)) return Status::failure(FailureCode::CaptureFailed);
            std::uint32_t index{};
            if(pointer) {
                auto found=std::find_if(colliders_.begin(),colliders_.end(),[&](const auto& item){return item.address==pointer;});
                if(found==colliders_.end()) return Status::failure(FailureCode::IdentityMismatch);
                index=static_cast<std::uint32_t>(found-colliders_.begin()+1);
            }
            std::memcpy(output.bytes.data()+cursor,&index,4);cursor+=4;
        }
    }
    if (cursor != image_bytes_) return Status::failure(FailureCode::CaptureFailed);
    output.checksum = Checksum(output);
    return Status::success();
}

Status MotionBankSnapshot::Capture(LocalReconstructionImage& output) noexcept
{
    return bound_ ? capture_unchecked(output)
        : Status::failure(FailureCode::AdapterUnqualified);
}

bool MotionBankSnapshot::write_unchecked(
    const LocalReconstructionImage& image, bool recovering) noexcept
{
    if (!ValidateLocalImage(image) || image.bytes.size() != image_bytes_ || image.context != context_
        || !topology_matches(!recovering) || !collision_values_valid(&image)) return false;
    std::uint64_t image_binding{};
    std::memcpy(&image_binding,image.bytes.data()+motion_bank_base_image_bytes-8,8);
    if(image_binding!=binding_fingerprint_) return false;
    for(unsigned player=0;player<2;++player)
        if(std::to_integer<unsigned>(image.bytes[8+player])>12) return false;
    std::size_t cursor = 10;
    std::size_t metadata{};
    for (std::size_t player = 0; player < 2; ++player)
    {
        for (std::size_t bank = 0; bank < 2; ++bank)
        {
            const auto& topology = topology_[player][bank];
            const auto current_slot =
                std::to_integer<std::uint8_t>(image.bytes[metadata++]);
            const auto provider_slot =
                std::to_integer<std::uint8_t>(image.bytes[metadata++]);
            if (current_slot >= 3 || provider_slot >= 3) return false;
            for (std::size_t slot = 0; slot < 3; ++slot)
            {
                if (!memory_.Write(topology.buffers[slot],
                        std::span{image.bytes}.subspan(cursor, topology.bytes)))
                    return false;
                cursor += topology.bytes;
            }
            const std::uint32_t active = current_slot;
            if (!write_value(memory_, topology.bank + 0x20, active)
                || !write_value(memory_, topology.bank + 0x28,
                    topology.buffers[current_slot])
                || !write_value(memory_, topology.bank + 0x30,
                    topology.buffers[provider_slot])) return false;
        }
        if (!memory_.Write(fighters_[player] + motion_tail_fighter_offset,
                std::span{image.bytes}.subspan(cursor, motion_tail_bytes)))
            return false;
        cursor += motion_tail_bytes;
    }
    for(unsigned player=0;player<2;++player)
        if(!write_value(memory_,fighters_[player]+0x2b3c8,
            resolve_secondary_source(std::to_integer<std::uint8_t>(image.bytes[8+player])))) return false;
    cursor+=8; // Binding fingerprint is local metadata, never native state.
    for (const auto& region : regions_) {
        if (!memory_.Write(region.address,std::span{image.bytes}.subspan(cursor,region.bytes))) return false;
        cursor += region.bytes;
    }
    for(auto node:collision_nodes_) {
        std::uint16_t count{};std::memcpy(&count,image.bytes.data()+cursor,2);cursor+=2;
        for(unsigned slot=0;slot<4;++slot) {
            std::uint32_t index{};std::memcpy(&index,image.bytes.data()+cursor,4);cursor+=4;
            const auto pointer=index?colliders_[index-1].address:std::uintptr_t{};
            if(!write_value(memory_,node+0x170+slot*0x60,pointer)) return false;
        }
        if(!write_value(memory_,node+0xda,count)) return false;
    }
    if (cursor != image_bytes_) return false;
    return true;
}

Status MotionBankSnapshot::RestoreTransactional(
    const LocalReconstructionImage& image) noexcept
{
    if (!ValidateLocalImage(image) || image.bytes.size() != image_bytes_ || image.context != context_
        || !topology_matches() || !collision_values_valid(&image))
        return Status::failure(FailureCode::RestorePreflightFailed);
    if (!capture_unchecked(undo_scratch_).ok())
        return Status::failure(FailureCode::CaptureFailed);
    if (write_unchecked(image))
    {
        if (capture_unchecked(observed_scratch_).ok()
            && observed_scratch_.bytes == image.bytes)
            return Status::success();
    }
    // A failed link write can leave the mutable count/pointer combination
    // temporarily inconsistent. Recover using the retained validated B image;
    // immutable owner/vtable/list guards still apply before any undo write.
    const bool undone = write_unchecked(undo_scratch_,true);
    if (!undone || !capture_unchecked(observed_scratch_).ok()
        || observed_scratch_.bytes != undo_scratch_.bytes)
        return Status::failure(FailureCode::UndoFailed);
    return Status::failure(FailureCode::RestoreWriteFailed);
}
}
