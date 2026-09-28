#include "Sc6ReplayWorldState.hpp"
#include "Sc6ReplayPhysicsMarkers.hpp"
#include "ReplayPhysicsMarkerGraph.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace Horse::Deterministic
{
namespace
{
template<class T> const T& ReadAt(const void* p, std::size_t offset)
{ return *reinterpret_cast<const T*>(static_cast<const std::byte*>(p) + offset); }
// Failure-only comparison of named fields; never compare C++ struct padding.
template<class T> bool BindingDifference(const T& a,const T& b,const char* name,
    Sc6ReplayWorldState::BindingDiagnostic* d,std::uintptr_t actor=0,std::size_t component=0) noexcept {
    if(a==b)return false;
    if(d) {
        const auto* left=reinterpret_cast<const unsigned char*>(&a);
        const auto* right=reinterpret_cast<const unsigned char*>(&b);
        for(std::size_t i=0;i<sizeof(T);++i)if(left[i]!=right[i]) {
            *d={name,actor,component,i,left[i],right[i]};break;
        }
    }
    return true;
}
void NormalizeInteractionStorage(Sc6ReplayWorldState::PhysicsBoundary::ActorObservation& a,
    const Sc6ReplayWorldState::PhysicsBoundary::ActorObservation& b) noexcept
{
    // 114150 copies only live entries and owns heap retirement. 114080
    // overwrites the next slot before publishing count; 114920 traverses count.
    // Retain current native backing. A rebuilt inactive marker may keep its
    // current address only with independently validated unchanged owners.
    if(!a.simulation || a.simulation!=b.simulation || a.interaction_count!=b.interaction_count
        || a.interaction_count>a.interactions.size()
        || ReadAt<std::uintptr_t>(a.simulation_storage.data(),0x40)!=ReadAt<std::uintptr_t>(b.simulation_storage.data(),0x40))return;
    for(unsigned side=0;side<2;++side) {
        const auto* row=side?&b:&a;
        const auto count=ReadAt<unsigned>(row->simulation_storage.data(),0x34);
        const auto capacity=ReadAt<unsigned>(row->simulation_storage.data(),0x30);
        const auto data=ReadAt<std::uintptr_t>(row->simulation_storage.data(),0x28);
        if(count!=row->interaction_count || count>capacity)return;
        if(!capacity) {if(data || count)return;}
        else if(capacity==4) {
            if(data!=row->simulation+8)return;
            for(unsigned i=0;i<count;++i)
                if(ReadAt<std::uintptr_t>(row->simulation_storage.data(),8+i*8)!=row->interactions[i].address)return;
        } else if(capacity<8 || capacity>64 || (capacity&(capacity-1)) || !data || (data&7)
            || (data>=row->simulation && data<row->simulation+row->simulation_storage.size()))return;
    }
    for(unsigned i=0;i<a.interaction_count;++i) {
        const auto& first=a.interactions[i]; const auto& second=b.interactions[i];
        if(first==second)continue;
        if(!first.marker_registered || !second.marker_registered
            || first.marker_elements!=second.marker_elements
            || first.marker_scene_count!=second.marker_scene_count
            || first.marker_scene_active!=second.marker_scene_active
            || first.marker_map_count!=second.marker_map_count
            || first.marker_actor_counts!=second.marker_actor_counts
            || first.marker_filter_valid!=second.marker_filter_valid
            || first.marker_attributes!=second.marker_attributes
            || first.marker_filter_data!=second.marker_filter_data
            || first.marker_shape_cores!=second.marker_shape_cores
            || first.marker_filter_pair!=0xffffffff || second.marker_filter_pair!=0xffffffff
            || first.header[0x24]!=std::byte{2} || second.header[0x24]!=std::byte{2}
            || first.header[0x25]!=std::byte{0xb} || second.header[0x25]!=std::byte{0xb}
            || first.header[0x26]!=std::byte{} || second.header[0x26]!=std::byte{}
            || std::memcmp(first.header.data(),second.header.data(),0x27))return;
        // Native133BA0 initializes bytes through +26, not padding +27.
        // Scene/actor inverse indices remain exact. No historical pointer write.
    }
    a.interactions=b.interactions;
    // Every inline active pointer was checked against the separately captured
    // active graph. In heap mode all inline slots are obsolete storage.
    std::memcpy(a.simulation_storage.data()+8,b.simulation_storage.data()+8,0x2c);
}
const void* NativeDelegate(const void* record)
{
    const auto* heap = ReadAt<const void*>(record, 0x30);
    return heap ? heap : static_cast<const std::byte*>(record) + 0x10;
}
bool Writable(const void* address, std::size_t bytes) noexcept
{
    auto current = reinterpret_cast<std::uintptr_t>(address);
    if (bytes > std::numeric_limits<std::uintptr_t>::max() - current) return false;
    const auto end = current + bytes;
    while (current < end)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(current), &region, sizeof(region))
            || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const auto protection = region.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY
            && protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
        if (next <= current) return false;
        current = next;
    }
    return true;
}
#include "Sc6ReplayWorldState.PhysicsNotifications.inl"
#include "Sc6ReplayWorldState.PhysicsNodes.inl"
#include "Sc6ReplayWorldState.PhysicsOrder.inl"
#include "Sc6ReplayWorldState.PhysicsSap.inl"
}
#include "Sc6ReplayWorldState.PhysicsMarkers.inl"

Sc6ReplayWorldState::~Sc6ReplayWorldState() { ClearRecords(); }

Status Sc6ReplayWorldState::ReadPhysicsNotifications(PhysicsBoundary& output,unsigned scene) noexcept
{
    if(scene>=output.scenes.size() || !output.valid_counts())return Status::failure(FailureCode::CapacityExceeded);
    std::uintptr_t sc{};
    for(unsigned i=0;i<output.observed_actors[scene];++i) {
        const auto& row=output.actors[scene][i];if(!row.simulation)continue;
        const auto owner=ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x40);
        if(sc && sc!=owner)return Status::failure(FailureCode::GenerationMismatch);
        sc=owner;
    }
    output.notifications[scene]={};
    if(!sc)return Status::success();
    if(!ReadPhysicsNotificationsForScene(output,scene,sc,output.notifications[scene])
        || !PhysicsNotificationsOwned(output,scene,output.notifications[scene]))
        return Status::failure(FailureCode::GenerationMismatch);
    output.node_domains[scene].notifications_quiescent=output.notifications[scene].Empty();
    return Status::success();
}

Status Sc6ReplayWorldState::ReadPhysicsShapeGeometry(const void* storage,
    PhysicsBoundary::ActorObservation::QueryShapeObservation& output) noexcept
{
    __try {
        output.geometry_kind=ReadAt<unsigned>(storage,0x68);
        output.geometry_bytes=output.geometry_kind==0?8:output.geometry_kind==2?12
            :output.geometry_kind==3?16:output.geometry_kind==4?41:output.geometry_kind==5?48:0;
        // Shipped PxTriangleMeshGeometryGeneratedValues at PhysX+A130 reads
        // scale+4..1F, flags+20 and mesh+28..2F. This is a borrowed identity
        // witness only. Capture does not grant mesh mutation or restoration.
        if(!output.geometry_bytes || output.geometry_bytes>output.geometry.size())
            return Status::failure(FailureCode::UnsupportedContent);
        std::memcpy(output.local_pose.data(),static_cast<const std::byte*>(storage)+0x40,output.local_pose.size());
        std::memcpy(output.geometry.data(),static_cast<const std::byte*>(storage)+0x68,output.geometry_bytes);
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::CaptureFailed);}
}

Status Sc6ReplayWorldState::ReadPhysicsBoundary(std::uintptr_t base, void* world, PhysicsBoundary& output) noexcept
{
    // Value-initialize in its existing allocation, without a full checkpoint
    // temporary on the native application thread's stack.
    std::construct_at(&output);
    __try
    {
        auto* owner = ReadAt<void*>(world, 0x1c8);
        if (!owner || ReadAt<void*>(owner, 0x160))
            return Status::failure(FailureCode::IllegalTransition);
        const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"PhysX3_x64.dll"));
        // PhysX Actors metadata wrapper 18000E3D0 copies PxU16 flags into
        // home space and passes their address to scene slot +90. The separate
        // Articulations count wrapper 18000E420 dispatches slot +B0.
        constexpr unsigned char count_code[]{0x48,0x83,0xec,0x28,0x0f,0xb7,0x02,0x48,0x8d,0x54,0x24,0x30,
            0x66,0x89,0x44,0x24,0x30,0x48,0x8b,0x01,0xff,0x90,0x90,0,0,0,0x48,0x83,0xc4,0x28,0xc3};
        constexpr unsigned char articulation_code[]{0x48,0x8b,0x01,0x48,0xff,0xa0,0xb0,0,0,0};
        if (!module || std::memcmp(reinterpret_cast<void*>(module + 0xe3d0), count_code, sizeof(count_code))
            || std::memcmp(reinterpret_cast<void*>(module + 0xe420), articulation_code, sizeof(articulation_code)))
            return Status::failure(FailureCode::IdentityMismatch);
        const auto read_dynamic = GetProcAddress(reinterpret_cast<HMODULE>(module), "??0PxRigidDynamicGeneratedValues@physx@@QEAA@PEBVPxRigidDynamic@1@@Z");
        const auto read_static = GetProcAddress(reinterpret_cast<HMODULE>(module), "??0PxRigidStaticGeneratedValues@physx@@QEAA@PEBVPxRigidStatic@1@@Z");
        if (reinterpret_cast<std::uintptr_t>(read_dynamic) != module + 0x7810
            || reinterpret_cast<std::uintptr_t>(read_static) != module + 0x7b80)
            return Status::failure(FailureCode::IdentityMismatch);
        output.owner = reinterpret_cast<std::uintptr_t>(owner);
        auto* render_lock = reinterpret_cast<CRITICAL_SECTION*>(static_cast<std::byte*>(world) + 0x270);
        EnterCriticalSection(render_lock);
        __try {
            for (unsigned set = 0; set < 2; ++set) {
                const auto* header = static_cast<std::byte*>(world) + 0x1d0 + set * 0x50;
                output.render_work_counts[set] = ReadAt<int>(header, 8) - ReadAt<int>(header, 0x34);
            }
        } __finally { LeaveCriticalSection(render_lock); }
        output.module = module;
        output.scene_count = ReadAt<std::uint32_t>(owner, 4);
        output.asynchronous = ReadAt<std::uint8_t>(owner, 0) != 0;
        if (output.scene_count > 3) return Status::failure(FailureCode::UnsupportedContent);
        output.scene_ids = {ReadAt<std::int16_t>(owner, 0xf8), ReadAt<std::int16_t>(owner, 0xfc)};
        for (std::size_t i = 0; i < 2; ++i)
        {
            if (i == 0 ? !output.scene_count : (!output.asynchronous || output.scene_count <= 2)) continue;
            auto* scene = reinterpret_cast<void* (*)(std::int16_t)>(base + 0x2046020)(output.scene_ids[i]);
            if (!scene) return Status::failure(FailureCode::GenerationMismatch);
            const auto* table = ReadAt<const std::uintptr_t*>(scene, 0);
            // Native 431D0/432B0 call Foundation lockWriter/unlockWriter.
            // These are scene +330/+338, also used by FinishPhysicsSceneFrame;
            // do not infer read/write slots from a different SDK version.
            if (table[0x330 / 8] != module + 0x431d0 || table[0x338 / 8] != module + 0x432b0)
                return Status::failure(FailureCode::IdentityMismatch);
            for (const auto offset : {0x18, 0x90, 0xb0, 0x330, 0x338})
            {
                MEMORY_BASIC_INFORMATION region{};
                if (!VirtualQuery(reinterpret_cast<void*>(table[offset / 8]), &region, sizeof(region))
                    || reinterpret_cast<std::uintptr_t>(region.AllocationBase) != module
                    || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
                    return Status::failure(FailureCode::IdentityMismatch);
            }
            output.scenes[i] = reinterpret_cast<std::uintptr_t>(scene);
            reinterpret_cast<void (*)(void*, const char*, unsigned)>(table[0x330 / 8])(scene, __FILE__, __LINE__);
            __try
            {
                // PxSceneGeneratedValues (18000A520) passes four-byte Flags
                // output to this virtual getter before the Limits field +4.
                reinterpret_cast<void* (*)(void*, std::uint32_t*)>(table[0x18 / 8])(scene, &output.scene_flags[i]);
                // Native SetGlobalPose uses NpScene +2450 for the two 30-byte
                // (hex) pruner records. CE5E0 selects by encoded handle bit 0;
                // CDEC0 appends dirty IDs; CEBF0 consumes them before commit.
                std::memcpy(output.query_pruners[i].data(), static_cast<std::byte*>(scene) + 0x2450, 0x60);
                // Static, dynamic, particle system, particle fluid, cloth,
                // and all known flags. Articulation links are not actors here.
                constexpr std::array<std::uint16_t, 6> flags{1, 2, 4, 8, 32, 47};
                for (std::size_t j = 0; j < flags.size(); ++j)
                    output.actor_counts[i][j] = reinterpret_cast<std::uint32_t (*)(void*, const std::uint16_t*)>(
                        module + 0xe3d0)(scene, &flags[j]);
                output.articulations[i] = reinterpret_cast<std::uint32_t (*)(void*)>(module + 0xe420)(scene);
                // Bounded inventory of the solver owners that blocked the
                // assembled transaction. Metadata constructors only call
                // native property getters; these are not restorable images.
                for (unsigned kind = 0; kind < 2; ++kind) {
                    const auto count = output.actor_counts[i][kind];
                    if (count > output.actors[i].size() - output.observed_actors[i])
                        return Status::failure(FailureCode::CapacityExceeded);
                    std::array<void*, 64> actors{};
                    const std::uint16_t mask = kind ? 2 : 1;
                    const auto read = reinterpret_cast<unsigned (*)(void*, const std::uint16_t*, void**, unsigned)>(
                        module + 0xe3a0)(scene, &mask, actors.data(), count);
                    if (read != count) return Status::failure(FailureCode::GenerationMismatch);
                    for (unsigned j = 0; j < count; ++j) {
                        auto& row = output.actors[i][output.observed_actors[i]++];
                        row.actor = reinterpret_cast<std::uintptr_t>(actors[j]);
                        row.vtable = ReadAt<std::uintptr_t>(actors[j], 0);
                        row.kind = mask;
                        reinterpret_cast<void* (*)(void*, void*)>(kind ? read_dynamic : read_static)(row.properties.data(), actors[j]);
                        // Keep owner identity available on a later shape-admission rejection.
                        row.user_data = ReadAt<std::uintptr_t>(row.properties.data(), 0x20);
                        // 142015B80 resolves FPhysxUserData type 1 to its
                        // FBodyInstance +8, then weak scene component +138.
                        if (row.user_data && ReadAt<int>(reinterpret_cast<void*>(row.user_data), 0) == 1) {
                            row.body = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(row.user_data), 8);
                            if (row.body) row.component = reinterpret_cast<std::uintptr_t>(
                                reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(reinterpret_cast<void*>(row.body + 0x138)));
                            if (row.component) {
                                std::memcpy(row.component_weak.data(), reinterpret_cast<void*>(row.body + 0x138), 8);
                                row.component_vtable = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(row.component), 0);
                            }
                        }
                        if (kind) {
                            if (row.vtable != module + 0x19b7c0)
                                return Status::failure(FailureCode::IdentityMismatch);
                            std::memcpy(row.dynamic_storage.data(), actors[j], row.dynamic_storage.size());
                            row.query_handle_count = ReadAt<std::uint16_t>(actors[j], 0x30);
                            if (row.query_handle_count > row.query_handles.size())
                                return Status::failure(FailureCode::CapacityExceeded);
                            const auto* handles = ReadAt<std::int16_t>(actors[j], 0x40) == 1
                                ? static_cast<std::byte*>(actors[j]) + 0x38 : ReadAt<const std::byte*>(actors[j], 0x38);
                            if (row.query_handle_count) std::memcpy(row.query_handles.data(), handles, row.query_handle_count * 8);
                            // 3C8F0 -> 56BE0 enumerates the actor's NpShape
                            // table at +28, inline for one shape. Query handles
                            // are a parallel table, not the ownership source.
                            const auto* shapes = row.query_handle_count == 1
                                ? reinterpret_cast<const std::uintptr_t*>(static_cast<std::byte*>(actors[j]) + 0x28)
                                : ReadAt<const std::uintptr_t*>(actors[j], 0x28);
                            for (unsigned h = 0; h < row.query_handle_count; ++h) {
                                const auto handle = row.query_handles[h];
                                auto& shape = row.query_shapes[h];
                                if (!shapes[h]) return Status::failure(FailureCode::GenerationMismatch);
                                // 3C0E0 passes NpShape+30 to Scb detach. This
                                // remains valid for simulation-only shapes.
                                shape.shape = shapes[h] + 0x30;
                                shape.body = row.actor + 0x60;
                                const void* bounds{};
                                if (handle != 0xffffffff) {
                                auto* pruner = ReadAt<void*>(scene, 0x2450 + (handle & 1) * 0x30);
                                const auto* pruner_table = ReadAt<const std::uintptr_t*>(pruner, 0);
                                const auto* payload = reinterpret_cast<const std::uintptr_t* (*)(void*, unsigned, const void**)>(
                                    pruner_table[0x48 / 8])(pruner, static_cast<unsigned>(handle >> 1), &bounds);
                                if (!payload || !bounds || payload[0] != shape.shape || payload[1] != shape.body)
                                    return Status::failure(FailureCode::GenerationMismatch);
                                }
                                const auto* storage = reinterpret_cast<const std::byte*>(shape.shape);
                                shape.control = ReadAt<unsigned>(storage, 8);
                                // D5850 consumes an unbuffered Scb::Shape local
                                // pose +40 and geometry +68. No inferred union
                                // size: retain only the supported primitive.
                                // 3C0E0 selects flags +60 unless control bit40
                                // selects buffered flags. Reject pending edits.
                                if (shape.control & 0x45) return Status::failure(FailureCode::UnsupportedContent);
                                shape.flags = ReadAt<std::uint8_t>(storage, 0x60);
                                if (bool(shape.flags & 2) != (handle != 0xffffffff))
                                    return Status::failure(FailureCode::GenerationMismatch);
                                // Convex metadata ctor 9F80 reads scale +4..1F,
                                // retained mesh +20 and flags +28. Admit that
                                // value witness only for an unchanged actor;
                                // it is not a convex mesh restoration payload.
                                const auto geometry=ReadPhysicsShapeGeometry(storage,shape);
                                if(!geometry.ok())return geometry;
                                if (bounds) std::memcpy(shape.bounds.data(), bounds, shape.bounds.size());
                            }
                            // SetGlobalPose 180039780 reaches core actor+80;
                            // 1800E0810 resolves its BodySim, whose +C0 pointer
                            // is read by wake admission 180112350. Kinematic
                            // state is core+B0, allocated as 64 bytes by E15C0.
                            row.simulation = ReadAt<std::uintptr_t>(actors[j], 0x80);
                            row.kinematic = ReadAt<std::uintptr_t>(actors[j], 0x130);
                            if (row.simulation) std::memcpy(row.simulation_storage.data(),
                                reinterpret_cast<void*>(row.simulation), row.simulation_storage.size());
                            if (row.kinematic) std::memcpy(row.kinematic_storage.data(),
                                reinterpret_cast<void*>(row.kinematic), row.kinematic_storage.size());
                            if (row.simulation) {
                                auto& admission = row.kinematic_admission;
                                admission.scene = ReadAt<std::uintptr_t>(row.simulation_storage.data(), 0x40);
                                const auto node_handle = ReadAt<unsigned>(row.simulation_storage.data(), 0xb0);
                                const auto active_index = ReadAt<unsigned>(row.simulation_storage.data(), 0xb8);
                                // Inactive bodies retain their existing strict byte comparison.
                                // Only bounded active handles need the additional admission witness.
                                if (admission.scene && active_index < 64 && (node_handle >> 6) < 4096) {
                                    const auto* sc = reinterpret_cast<const void*>(admission.scene);
                                    admission.controller = ReadAt<std::uintptr_t>(sc, 0x770);
                                    admission.island_owner = ReadAt<std::uintptr_t>(sc, 0x748);
                                    if (!admission.controller || !admission.island_owner)
                                        return Status::failure(FailureCode::GenerationMismatch);
                                    admission.controller_vtable = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(admission.controller), 0);
                                    admission.controller_owner = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(admission.controller), 8);
                                    if (admission.controller_vtable == module + 0x1ab0e8) {
                                        constexpr unsigned char no_op[]{0xc2, 0, 0};
                                        if (ReadAt<std::uintptr_t>(reinterpret_cast<void*>(admission.controller_vtable), 0x28) != module + 0x119550
                                            || std::memcmp(reinterpret_cast<void*>(module + 0x119550), no_op, sizeof(no_op)))
                                            return Status::failure(FailureCode::IdentityMismatch);
                                    }
                                    std::memcpy(admission.active_header.data(), reinterpret_cast<void*>(admission.scene + 0x20), 0x18);
                                    const auto count = ReadAt<unsigned>(admission.active_header.data(), 8);
                                    const auto capacity = ReadAt<unsigned>(admission.active_header.data(), 12) & 0x7fffffff;
                                    const auto prefix = ReadAt<unsigned>(admission.active_header.data(), 16);
                                    const auto storage = ReadAt<std::uintptr_t>(admission.active_header.data(), 0);
                                    if (!PhysicsActiveBodyIndexValid(std::to_integer<unsigned>(row.properties[0x9c]),
                                        count,capacity,prefix,active_index,storage))
                                        return Status::failure(FailureCode::UnsupportedContent);
                                    std::memcpy(admission.active_bodies.data(), reinterpret_cast<void*>(storage), count * 8);
                                    if (admission.active_bodies[active_index] != row.actor + 0x80)
                                        return Status::failure(FailureCode::GenerationMismatch);
                                    for (unsigned slot = 0; slot < 2; ++slot) {
                                        auto& island = admission.islands[slot];
                                        const auto sim = admission.island_owner + (slot ? 0x310 : 0xb0);
                                        std::memcpy(island.storage_header.data(), reinterpret_cast<void*>(sim + 0x18), 0x20);
                                        const auto nodes = ReadAt<std::uintptr_t>(island.storage_header.data(), 0);
                                        const auto indices = ReadAt<std::uintptr_t>(island.storage_header.data(), 0x10);
                                        if (!nodes || !indices) return Status::failure(FailureCode::GenerationMismatch);
                                        std::memcpy(island.node.data(), reinterpret_cast<void*>(nodes + (node_handle >> 6) * 0x20), 0x20);
                                        island.index = ReadAt<unsigned>(reinterpret_cast<void*>(indices), (node_handle >> 6) * 4);
                                        for (unsigned list = 0; list < 2; ++list) {
                                            auto& header = island.list_headers[list];
                                            std::memcpy(header.data(), reinterpret_cast<void*>(sim + (list ? 0x190 : 0x98)), 0x10);
                                            const auto list_count = ReadAt<unsigned>(header.data(), 8);
                                            const auto list_capacity = ReadAt<unsigned>(header.data(), 12) & 0x7fffffff;
                                            const auto list_storage = ReadAt<std::uintptr_t>(header.data(), 0);
                                            if (list_count > 64 || list_count > list_capacity || (list_count && !list_storage))
                                                return Status::failure(FailureCode::CapacityExceeded);
                                            if (list_count) std::memcpy(island.lists[list].data(), reinterpret_cast<void*>(list_storage), list_count * 4);
                                        }
                                    }
                                    admission.valid = true;
                                }
                                row.interaction_count = ReadAt<unsigned>(row.simulation_storage.data(), 0x34);
                                if (row.interaction_count > row.interactions.size())
                                    return Status::failure(FailureCode::CapacityExceeded);
                                const auto* interactions = ReadAt<const std::uintptr_t*>(row.simulation_storage.data(), 0x28);
                                for (unsigned entry = 0; entry < row.interaction_count; ++entry) {
                                    auto& interaction = row.interactions[entry];
                                    interaction.address = interactions[entry];
                                    // 114920 dispatches on Interaction +24;
                                    // +25 carries dirty-list membership flags.
                                    std::memcpy(interaction.header.data(), reinterpret_cast<void*>(interaction.address), interaction.header.size());
                                    if (ReadAt<std::uintptr_t>(interaction.header.data(),0)==module+0x1ab300
                                        && interaction.header[0x24]==std::byte{2}) {
                                        // Native120030 owns two ElementSim pointers and the filter-pair
                                        // index. Native135FB0/11C480 retire all three memberships.
                                        const auto* marker=reinterpret_cast<const void*>(interaction.address);
                                        std::memcpy(interaction.marker_elements.data(),static_cast<const std::byte*>(marker)+0x28,16);
                                        interaction.marker_filter_pair=ReadAt<unsigned>(marker,0x38);
                                        const auto actor0=ReadAt<std::uintptr_t>(marker,8),actor1=ReadAt<std::uintptr_t>(marker,16);
                                        const auto physics_scene=ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x40);
                                        const auto& elements=interaction.marker_elements;
                                        if(elements[0] && elements[1] && elements[0]!=elements[1]
                                            && actor0 && actor1 && actor0!=actor1
                                            && (row.simulation==actor0 || row.simulation==actor1)
                                            && ReadAt<unsigned>(marker,row.simulation==actor0?0x1c:0x20)==entry
                                            && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(elements[0]),0x10)==actor0
                                            && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(elements[1]),0x10)==actor1
                                            && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(actor0),0x40)==physics_scene
                                            && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(actor1),0x40)==physics_scene) {
                                            const auto* native_scene=reinterpret_cast<const void*>(physics_scene);
                                            const auto index=ReadAt<unsigned>(marker,0x18);
                                            const auto count=ReadAt<unsigned>(native_scene,0x60);
                                            const auto capacity=ReadAt<unsigned>(native_scene,0x64);
                                            const auto data=ReadAt<std::uintptr_t>(native_scene,0x58);
                                            const auto nphase=ReadAt<std::uintptr_t>(native_scene,0x1058);
                                            interaction.marker_scene_count=count;
                                            interaction.marker_scene_active=ReadAt<unsigned>(native_scene,0x70);
                                            interaction.marker_actor_counts={ReadAt<unsigned>(reinterpret_cast<void*>(actor0),0x34),
                                                ReadAt<unsigned>(reinterpret_cast<void*>(actor1),0x34)};
                                            bool filters_valid=true;
                                            for(unsigned element=0;element<2;++element) {
                                                auto* shape=reinterpret_cast<void*>(elements[element]);
                                                constexpr unsigned char filter_getter_signature[]{0x45,0x33,0xc9,0x4d,0x8b,0xd8,0x44,0x89,0x0a,0x41,0xb8,0x20,0,0,0};
                                                if(ReadAt<std::uintptr_t>(shape,0)!=module+0x1aae40
                                                    || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(module+0x1aae40),8)!=module+0x1143e0
                                                    || std::memcmp(reinterpret_cast<void*>(module+0x1143e0),filter_getter_signature,sizeof(filter_getter_signature))) {filters_valid=false;break;}
                                                interaction.marker_shape_cores[element]=ReadAt<std::uintptr_t>(shape,0x40);
                                                reinterpret_cast<void (*)(void*,unsigned*,void*)>(module+0x1143e0)(shape,
                                                    &interaction.marker_attributes[element],interaction.marker_filter_data[element].data());
                                            }
                                            interaction.marker_filter_valid=filters_valid;
                                            // 123110 erases from the dense 24-byte element-pair map;
                                            // +34 is its live size. Validate current registration,
                                            // without capturing or publishing map allocation history.
                                            if(data && nphase && index<count && count<=capacity && count<=4096
                                                && ReadAt<unsigned>(native_scene,0x70)<=index
                                                && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(data),index*8)==interaction.address) {
                                                const auto* map=reinterpret_cast<void*>(nphase+0x1558);
                                                const auto entries=ReadAt<std::uintptr_t>(map,8);
                                                const auto size=ReadAt<unsigned>(map,0x34);
                                                interaction.marker_map_count=size;
                                                const auto low=elements[0]<elements[1]?elements[0]:elements[1];
                                                const auto high=elements[0]<elements[1]?elements[1]:elements[0];
                                                unsigned matches=0,keys=0;
                                                if(entries && size<=4096)for(unsigned pair=0;pair<size;++pair) {
                                                    const auto* record=reinterpret_cast<void*>(entries+pair*24);
                                                    if(ReadAt<std::uintptr_t>(record,0)==low && ReadAt<std::uintptr_t>(record,8)==high) {
                                                        ++keys;
                                                        if(ReadAt<std::uintptr_t>(record,16)==interaction.address)++matches;
                                                    }
                                                }
                                                interaction.marker_registered=keys==1 && matches==1;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        if (IsolatedPhysicsCore(row,module)) {
                            // 112750 skips createSqBounds for flags3. 112810
                            // still visits destroySqBounds, which must be inert:
                            // every ordinary ShapeSim has invalid +4C index.
                            auto shape=ReadAt<std::uintptr_t>(row.simulation_storage.data(),0x38);
                            unsigned visited{};bool idle=true;
                            while(shape && visited++<32) {
                                const auto* element=reinterpret_cast<void*>(shape);
                                if(ReadAt<std::uintptr_t>(element,0x10)!=row.simulation
                                    || (!(ReadAt<unsigned>(element,0x18)&0x60000000)
                                        && ReadAt<unsigned>(element,0x4c)!=0xffffffffu)) {idle=false;break;}
                                shape=ReadAt<std::uintptr_t>(element,8);
                            }
                            row.isolated_kinematic=idle && !shape;
                        }
                        if (row.body) std::memcpy(row.body_scale.data(), reinterpret_cast<void*>(row.body + 4), 12);
                        if (row.component) {
                            // SC6 142028750 compares the scene component's
                            // world transform at +270 before pose publication.
                            std::memcpy(row.component_transform.data(), reinterpret_cast<void*>(row.component + 0x270), row.component_transform.size());
                            const auto* component = reinterpret_cast<const std::byte*>(row.component);
                            std::size_t cursor{};
                            for (const auto span : {std::pair{0x24cu, 28u}, {0x2a0u, 28u}, {0x2c0u, 24u}, {0x2e0u, 28u}, {0x300u, 12u}}) {
                                std::memcpy(row.component_transform_auxiliary.data() + cursor, component + span.first, span.second);
                                cursor += span.second;
                            }
                            row.component_parent = ReadAt<std::uintptr_t>(component, 0x1d0);
                            row.component_children_storage = ReadAt<std::uintptr_t>(component, 0x1d8);
                            row.component_child_count = ReadAt<std::int32_t>(component, 0x1e0);
                            row.component_child_capacity = ReadAt<std::int32_t>(component, 0x1e4);
                            row.component_socket = ReadAt<std::uint64_t>(component, 0x238);
                            row.component_flags = ReadAt<std::uint32_t>(component, 0x188);
                            row.component_transform_flags = ReadAt<std::uint32_t>(component, 0x240);
                            row.component_scopes = ReadAt<std::int32_t>(component, 0x3d8);
                            if (row.component_child_count < 0 || row.component_child_count > row.component_children.size()
                                || row.component_child_capacity < row.component_child_count)
                                return Status::failure(FailureCode::CapacityExceeded);
                            if (row.component_child_count) std::memcpy(row.component_children.data(),
                                reinterpret_cast<void*>(row.component_children_storage), row.component_child_count * 8);
                        }
                    }
                }
            }
            __finally { reinterpret_cast<void (*)(void*)>(table[0x338 / 8])(scene); }
        }
        // 1403B0AB0 constructs these under ALuxBattleChara and appends +3B0.
        // Native class-chain comparison mirrors that function's IsA test.
        const auto* expected_class = reinterpret_cast<void* (*)()>(base + 0x8e3ca0)();
        const auto class_depth = ReadAt<int>(expected_class, 0x90);
        if (class_depth < 0 || class_depth > 64) return Status::failure(FailureCode::GenerationMismatch);
        std::array<std::uintptr_t, 128> owners{};
        std::size_t owner_count{};
        for (unsigned s = 0; s < output.scenes.size(); ++s)
            for (unsigned a = 0; a < output.observed_actors[s]; ++a) {
                const auto component = output.actors[s][a].component;
                if (!component) continue;
                const auto owner = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(component), 0x190);
                if (!owner) continue;
                bool seen{};
                for (std::size_t n = 0; n < owner_count; ++n) seen |= owners[n] == owner;
                if (seen) continue;
                owners[owner_count++] = owner;
                const auto* actual_class = ReadAt<const void*>(reinterpret_cast<void*>(owner), 0x10);
                if (ReadAt<int>(actual_class, 0x90) < class_depth
                    || ReadAt<const std::uintptr_t*>(actual_class, 0x88)[class_depth]
                        != reinterpret_cast<std::uintptr_t>(expected_class) + 0x88) continue;
                const auto storage = ReadAt<std::uintptr_t>(reinterpret_cast<void*>(owner), 0x3b0);
                const auto count = ReadAt<int>(reinterpret_cast<void*>(owner), 0x3b8);
                const auto capacity = ReadAt<int>(reinterpret_cast<void*>(owner), 0x3bc);
                // Native141D1DC80 returns the fighter's inline +390 weapon slot.
                // Its proxy is recreated by the same skinned-mesh visibility path.
                const auto weapon=ReadAt<std::uintptr_t>(reinterpret_cast<void*>(owner),0x390);
                if (count < 0 || capacity < count || capacity > 128 || (count && !storage)
                    || output.creation_owner_count + count + (weapon?1:0) > output.creation_owners.size())
                    return Status::failure(FailureCode::CapacityExceeded);
                for (int n = 0; n < count + (weapon?1:0); ++n) {
                    const bool weapon_slot=n==count;
                    auto& entry = output.creation_owners[output.creation_owner_count++];
                    entry.membership=weapon_slot?ReplayCreationRenderOwner::Membership::WeaponSlot:ReplayCreationRenderOwner::Membership::CreationArray;
                    entry.component = weapon_slot?weapon:reinterpret_cast<const std::uintptr_t*>(storage)[n];
                    if (!entry.component) return Status::failure(FailureCode::GenerationMismatch);
                    entry.owner = owner; entry.storage = weapon_slot?owner+0x390:storage; entry.slot = weapon_slot?0:n;
                    entry.count = weapon_slot?1:count; entry.capacity = weapon_slot?1:capacity;
                    const auto* mesh = reinterpret_cast<void*>(entry.component);
                    for(std::size_t prior=0;prior+1<output.creation_owner_count;++prior)
                        if(output.creation_owners[prior].component==entry.component) return Status::failure(FailureCode::GenerationMismatch);
                    if (ReadAt<std::uintptr_t>(mesh, 0x190) != owner
                        || ReadAt<std::uintptr_t>(mesh,0)!=base+0x38829c0)
                        return Status::failure(FailureCode::GenerationMismatch);
                    entry.parent = ReadAt<std::uintptr_t>(mesh, 0x1d0);
                    entry.flags = ReadAt<unsigned>(mesh, 0x188);
                    entry.visibility = ReadAt<unsigned>(mesh, 0x240);
                    entry.primitive_id = ReadAt<unsigned>(mesh, 0x420);
                    entry.mesh = ReadAt<std::uintptr_t>(mesh, 0x910);
                    entry.animation = ReadAt<std::uintptr_t>(mesh, 0xab0);
                    reinterpret_cast<void (*)(void*, const void*)>(base + 0xf7bad0)(entry.weak.data(), mesh);
                    reinterpret_cast<void (*)(void*, const void*)>(base + 0xf7bad0)(entry.owner_weak.data(), reinterpret_cast<void*>(owner));
                    if (reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(entry.weak.data()) != mesh
                        || reinterpret_cast<std::uintptr_t>(reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0)(entry.owner_weak.data())) != owner)
                        return Status::failure(FailureCode::GenerationMismatch);
                }
            }
        for(unsigned scene=0;scene<output.scenes.size();++scene) {
            if(!ReadPhysicsNodeDomain(output,scene))return Status::failure(FailureCode::UnsupportedContent);
            if(!ReadPhysicsNotifications(output,scene).ok())return Status::failure(FailureCode::UnsupportedContent);
            std::uintptr_t sc{};
            for(unsigned i=0;i<output.observed_actors[scene];++i) {
                const auto& actor=output.actors[scene][i];if(!actor.simulation)continue;
                const auto owner=ReadAt<std::uintptr_t>(actor.simulation_storage.data(),0x40);
                if(sc && sc!=owner)return Status::failure(FailureCode::GenerationMismatch);
                sc=owner;
            }
            // Captured unsupported ownership is explicit and is rejected by
            // the seek transaction before publication, never normalized away.
            if(sc)ReadPhysicsSap(module,sc,output.broadphases[scene],output.scenes[scene]);
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::PreparePhysicsProjection(const PhysicsBoundary& target, const PhysicsBoundary& original,
    PhysicsProjectionFailure* failure, bool owned_render_work, const Sc6ReplayPhysicsMarkers* markers, bool planned_creation_visibility) noexcept
{
    unsigned scene_index{}, actor_index{};
    if (failure) *failure = {};
    const auto reject = [&](FailureCode code, const char* check) {
        if (failure) *failure = {check, scene_index, actor_index};
        return Status::failure(code);
    };
    if (!target.valid_counts() || !original.valid_counts())
        return reject(FailureCode::CapacityExceeded, "observation_counts");
    if (target.render_work_counts != std::array<std::int32_t, 2>{}
        || (!owned_render_work && original.render_work_counts != std::array<std::int32_t, 2>{}))
        return reject(FailureCode::IllegalTransition, "pending_render_work");
    if (target.owner != original.owner || target.module != original.module || target.scenes != original.scenes
        || target.scene_ids != original.scene_ids || target.scene_count != original.scene_count
        || target.asynchronous != original.asynchronous || target.actor_counts != original.actor_counts
        || target.articulations != original.articulations || target.scene_flags != original.scene_flags
        || target.observed_actors != original.observed_actors)
        return reject(FailureCode::GenerationMismatch, "scene_membership");
    for (unsigned scene = 0; scene < target.scenes.size(); ++scene) {
        scene_index = scene;
        const auto& af=target.node_domains[scene];const auto& bf=original.node_domains[scene];
        if(!PhysicsNotificationPairValid(target,original,scene))
            return reject(FailureCode::GenerationMismatch,"physics_notification_ownership");
        if(af.filter_shader!=bf.filter_shader || af.filter_callback!=bf.filter_callback
            || af.filter_data!=bf.filter_data || af.filter_data_size!=bf.filter_data_size)
            return reject(FailureCode::GenerationMismatch,"physics_filter_binding");
        if((target.node_domains[scene].valid || original.node_domains[scene].valid)
            && !PhysicsNodePairValid(target,original,scene))return reject(FailureCode::GenerationMismatch,"physics_node_ownership");
        if (target.articulations[scene] || target.actor_counts[scene][2] || target.actor_counts[scene][3]
            || target.actor_counts[scene][4]) return reject(FailureCode::UnsupportedContent, "solver_kind");
        auto query = target.query_pruners[scene];
        for (unsigned pruner = 0; pruner < 2; ++pruner) {
            const auto offset = pruner * 0x30;
            if (ReadAt<unsigned>(query.data(), offset + 0x20)
                || ReadAt<unsigned>(original.query_pruners[scene].data(), offset + 0x20))
                return reject(FailureCode::IllegalTransition, "pending_queries");
            // Query invalidation timestamps are process-local cache epochs,
            // not replay time. Reconstruction advances them; never rewind.
            std::memcpy(query.data() + offset + 0x2c, original.query_pruners[scene].data() + offset + 0x2c, 4);
        }
        if (query != original.query_pruners[scene]) return reject(FailureCode::GenerationMismatch, "query_storage");
        const bool reorder=PhysicsOrderDiffers(target,original,scene);
        const char* order_failure{};
        if(reorder && !PhysicsOrderPairValid(target,original,scene,&order_failure))
            return reject(FailureCode::UnsupportedContent,order_failure?order_failure:"physics_order_not_retained_permutation");
        unsigned query_shapes{};
        for (unsigned actor = 0; actor < target.observed_actors[scene]; ++actor) {
            actor_index = actor;
            auto normalized = target.actors[scene][actor];
            const auto& a = target.actors[scene][actor];
            const auto& b = original.actors[scene][actor];
            if (owned_render_work) normalized.component_flags =
                (normalized.component_flags & ~0xc0000060u) | (b.component_flags & 0xc0000060u);
            if(reorder && (a.kinematic_admission.valid || b.kinematic_admission.valid)) {
                normalized.kinematic_admission=b.kinematic_admission;
                std::memcpy(normalized.simulation_storage.data()+0xb8,b.simulation_storage.data()+0xb8,4);
            }
            if (a.kind == 1) {
                if (normalized != b) return reject(FailureCode::UnsupportedContent, "static_actor_changed");
                continue;
            }
            bool immutable_convex{};
            for (unsigned h = 0; h < a.query_handle_count; ++h)
                immutable_convex = immutable_convex || a.query_shapes[h].geometry_kind == 4 || a.query_shapes[h].geometry_kind == 5;
            if (immutable_convex) {
                if (normalized != b) return reject(FailureCode::UnsupportedContent, "convex_actor_changed");
                for (unsigned h = 0; h < a.query_handle_count; ++h)
                    if (a.query_handles[h] != 0xffffffff || a.query_shapes[h].flags != 8)
                        return reject(FailureCode::UnsupportedContent, "convex_actor_admission");
                continue;
            }
            if (a.kind != 2 || !a.component || !a.body || !a.simulation || !a.kinematic
                || a.component_scopes || b.component_scopes || ReadAt<unsigned char>(a.properties.data(), 0x9c) != 3
                || ReadAt<unsigned char>(b.properties.data(), 0x9c) != 3
                || ReadAt<unsigned>(a.dynamic_storage.data(), 0x68) != 0x83000000
                || ReadAt<unsigned>(b.dynamic_storage.data(), 0x68) != 0x83000000
                || ReadAt<unsigned>(a.dynamic_storage.data(), 0x17c) || ReadAt<unsigned>(b.dynamic_storage.data(), 0x17c)
                || a.kinematic_storage[0x1f] != std::byte{1} || b.kinematic_storage[0x1f] != std::byte{1})
                return reject(FailureCode::UnsupportedContent, "kinematic_owner_or_admission");
            for (unsigned entry = 0; entry < a.interaction_count; ++entry) {
                const auto& header = a.interactions[entry].header;
                // Native 114920 handles contact=0, trigger=1, particle=5.
                // This experiment admits only unchanged inactive marker=2
                // and constraint=4 records; no contact/trigger cache repair.
                if ((header[0x24] != std::byte{2} && header[0x24] != std::byte{4})
                    || (std::to_integer<unsigned>(header[0x25]) & 0x70) || header[0x26] != std::byte{})
                    return reject(FailureCode::UnsupportedContent, "active_interaction");
            }
            for (unsigned handle = 0; handle < a.query_handle_count; ++handle)
                if (a.query_handles[handle] != 0xffffffff) {
                    if (!(a.query_handles[handle] & 1)) return reject(FailureCode::UnsupportedContent, "query_handle_kind");
                    normalized.query_shapes[handle].bounds = b.query_shapes[handle].bounds;
                    ++query_shapes;
                }
                else if (!a.query_shapes[handle].shape || (a.query_shapes[handle].flags & 2))
                    return reject(FailureCode::GenerationMismatch, "simulation_only_shape_owner");
            for (const auto span : {std::pair{0x90u, 28u}, {0xd0u, 12u}, {0xe0u, 12u}, {0x140u, 52u}})
                std::memcpy(normalized.dynamic_storage.data() + span.first, b.dynamic_storage.data() + span.first, span.second);
            const auto moved_a = ReadAt<unsigned short>(a.simulation_storage.data(), 0xb4);
            const auto moved_b = ReadAt<unsigned short>(b.simulation_storage.data(), 0xb4);
            const bool isolated=IsolatedPhysicsKinematic(target,scene,actor) && IsolatedPhysicsKinematic(original,scene,actor);
            if (moved_a != moved_b || a.kinematic_storage[0x1c] != b.kinematic_storage[0x1c]) {
                // 119460 constructs the CPU controller; its +28 slot is the
                // three-byte RET at 119550. No controller payload is repaired.
                const auto& admission = a.kinematic_admission.valid?a.kinematic_admission:b.kinematic_admission;
                if (!admission.valid || (!reorder && a.kinematic_admission != b.kinematic_admission)
                    || admission.controller_vtable != target.module + 0x1ab0e8
                    || ((moved_a ^ moved_b) & ~((isolated?0x604u:4u)
                        | (target.notifications[scene].valid && original.notifications[scene].valid?0xf0u:0u)))
                    || std::to_integer<unsigned>(a.kinematic_storage[0x1c]) > 1
                    || std::to_integer<unsigned>(b.kinematic_storage[0x1c]) > 1
                    || bool(moved_a & 4) != bool(std::to_integer<unsigned>(a.kinematic_storage[0x1c]))
                    || bool(moved_b & 4) != bool(std::to_integer<unsigned>(b.kinematic_storage[0x1c])))
                    return reject(FailureCode::UnsupportedContent, "kinematic_wake_admission");
                for (const auto& island : admission.islands) {
                    // 147330 skips list insertion for flag2 and finishes by
                    // clearing 41. Require that operation to be a true no-op.
                    const auto flags = std::to_integer<unsigned>(island.node[4]);
                    if (!isolated && (!(flags & 2) || (flags & 0x41)))
                        return reject(FailureCode::UnsupportedContent, "kinematic_island_activation");
                }
                std::memcpy(normalized.simulation_storage.data() + 0xb4, b.simulation_storage.data() + 0xb4, 2);
                normalized.kinematic_storage[0x1c] = b.kinematic_storage[0x1c];
            }
            std::memcpy(normalized.kinematic_storage.data(), b.kinematic_storage.data(), 28);
            if(std::memcmp(a.simulation_storage.data()+0x60,b.simulation_storage.data()+0x60,28)) {
                // Native111AC0/1122C0 own the seven-word previous transform.
                // CCD pointers and every other low-level solver field stay exact.
                for(const auto* row:{&a,&b})
                    if(ReadAt<std::uintptr_t>(row->simulation_storage.data(),0)!=target.module+0x1aadc0
                        || ReadAt<std::uintptr_t>(row->simulation_storage.data(),0x48)!=row->actor+0x80
                        || ReadAt<std::uintptr_t>(row->simulation_storage.data(),0x88)!=row->actor+0x90
                        || ReadAt<std::uintptr_t>(row->simulation_storage.data(),0x80))
                        return reject(FailureCode::UnsupportedContent,"previous_transform_owner");
                std::memcpy(normalized.simulation_storage.data()+0x60,b.simulation_storage.data()+0x60,28);
            }
            NormalizeInteractionStorage(normalized, b);
            if(markers && markers->NormalizePreflight(target,original,scene,actor,normalized)
                && a.isolated_kinematic!=b.isolated_kinematic) {
                // This captured admission witness is derived, never a native
                // write. A registered marker alone makes IsolatedPhysicsCore
                // false. Normalize only that explained count transition after
                // the marker transaction has proved both registration graphs.
                // `isolated` above and Install still use the original A/B
                // witnesses; this must not enable wake/node-state projection.
                if((a.interaction_count && !a.isolated_kinematic && b.isolated_kinematic
                        && IsolatedPhysicsCore(b,target.module))
                    || (b.interaction_count && !b.isolated_kinematic && a.isolated_kinematic
                        && IsolatedPhysicsCore(a,target.module)))
                    normalized.isolated_kinematic=b.isolated_kinematic;
            }
            if(isolated) {
                // 3B060/3B110 read the public sleeping/wake values directly
                // from actor+178/+174, not from the island inverse index.
                // 14EE0/3E4A0 publish these alongside Core wake actor+11C.
                // Preserve both captured domains without issuing activation.
                for(const auto* row:{&a,&b}) {
                    const auto sleeping=ReadAt<unsigned>(row->dynamic_storage.data(),0x178);
                    if(sleeping>1 || row->properties[0xbc]!=std::byte(sleeping)
                        || std::memcmp(row->properties.data()+0xcc,row->dynamic_storage.data()+0x174,4))
                        return reject(FailureCode::GenerationMismatch,"kinematic_public_wake_witness");
                }
                std::memcpy(normalized.dynamic_storage.data()+0x11c,b.dynamic_storage.data()+0x11c,4);
                std::memcpy(normalized.dynamic_storage.data()+0x174,b.dynamic_storage.data()+0x174,8);
                normalized.properties[0xbc]=b.properties[0xbc];
                std::memcpy(normalized.properties.data()+0xcc,b.properties.data()+0xcc,4);
            }
            std::memcpy(normalized.properties.data() + 0x28, b.properties.data() + 0x28, 28);
            std::memcpy(normalized.properties.data() + 0x84, b.properties.data() + 0x84, 24);
            // Native 142006850 compares requested scale with FBodyInstance
            // +4/+8/+C using epsilon before updating geometry. Component-world
            // scale can differ by one ULP with identical body scale/shapes.
            // Restore that component value exactly; require actual body scale,
            // geometry and local shape poses unchanged in the record comparison.
            if (std::memcmp(a.component_transform_auxiliary.data() + 108, b.component_transform_auxiliary.data() + 108, 12))
                return reject(FailureCode::UnsupportedContent, "shape_scale_changed");
            normalized.component_transform = b.component_transform;
            normalized.component_transform_auxiliary = b.component_transform_auxiliary;
            // Native141DB0600 consumes/sets bit0 as ComponentToWorld current.
            // It belongs to the restored cache, not to immutable component
            // identity. All visibility/absolute-channel/other bits stay exact.
            normalized.component_transform_flags = (normalized.component_transform_flags&~1u)
                | (b.component_transform_flags&1u);
            if(planned_creation_visibility && ((a.component_transform_flags^b.component_transform_flags)&0x10u)) {
                // Initial host preparation precedes native visibility publication.
                // Only the existing retained owner transaction may cover this
                // difference. Install/Validate never use this preparation mode.
                const ReplayCreationRenderOwner *left{},*right{};
                for(unsigned i=0;i<target.creation_owner_count;++i)
                    if(target.creation_owners[i].component==a.component) {
                        if(left)return reject(FailureCode::GenerationMismatch,"duplicate_visibility_owner");
                        left=&target.creation_owners[i];
                    }
                for(unsigned i=0;i<original.creation_owner_count;++i)
                    if(original.creation_owners[i].component==b.component) {
                        if(right)return reject(FailureCode::GenerationMismatch,"duplicate_visibility_owner");
                        right=&original.creation_owners[i];
                    }
                if(!left || !right || !left->SameBinding(*right)
                    || !left->ValidMembership(left->storage,left->count,left->capacity,a.component)
                    || left->visibility!=a.component_transform_flags || right->visibility!=b.component_transform_flags)
                    return reject(FailureCode::GenerationMismatch,"unowned_component_visibility");
                normalized.component_transform_flags=(normalized.component_transform_flags&~0x10u)|(b.component_transform_flags&0x10u);
            }
            if (normalized != b) return reject(FailureCode::GenerationMismatch,
                normalized.dynamic_storage != b.dynamic_storage ? "actor_unaudited_field"
                : normalized.simulation_storage != b.simulation_storage ? "body_sim_changed"
                : normalized.kinematic_storage != b.kinematic_storage ? "kinematic_admission_changed"
                : normalized.properties != b.properties ? "actor_property_changed"
                : normalized.query_shapes != b.query_shapes ? "shape_inputs_changed"
                : normalized.component_flags != b.component_flags ? "component_flags_changed"
                : normalized.component_transform_flags != b.component_transform_flags ? "component_transform_flags_changed"
                : normalized.body_scale != b.body_scale ? "body_scale_changed" : "component_or_interaction_binding");
        }
        // Reconstruct in bounded batches; total shape count need not fit the
        // native dirty list simultaneously. A zero-capacity list still rejects.
        if (query_shapes && !(ReadAt<unsigned>(original.query_pruners[scene].data(), 0x54) & 0x7fffffff))
            return reject(FailureCode::CapacityExceeded, "query_reconstruction_capacity");
    }
    return Status::success();
}

Status Sc6ReplayWorldState::ValidatePhysicsProjection(const PhysicsBoundary& expected, const PhysicsBoundary& observed, bool queries_ready, bool owned_render_work) noexcept
{
    const auto status = PreparePhysicsProjection(expected, observed, nullptr, owned_render_work);
    if (!status.ok()) return status;
    for(unsigned scene=0;scene<expected.scenes.size();++scene)
        if(!PhysicsNodeAllocationEqual(expected.node_domains[scene],observed.node_domains[scene])
            || expected.notifications[scene]!=observed.notifications[scene])return Status::failure(FailureCode::RestoreVerificationFailed);
    for (unsigned scene = 0; scene < expected.scenes.size(); ++scene)
        for (unsigned i = 0; i < expected.observed_actors[scene]; ++i) {
            auto actual = observed.actors[scene][i];
            if (owned_render_work) actual.component_flags =
                (actual.component_flags & ~0xc0000060u) | (expected.actors[scene][i].component_flags & 0xc0000060u);
            if (!queries_ready) for (unsigned h = 0; h < actual.query_handle_count; ++h)
                actual.query_shapes[h].bounds = expected.actors[scene][i].query_shapes[h].bounds;
            if(actual.kinematic_admission.valid)for(unsigned slot=0;slot<2;++slot)for(const auto offset:{0u,16u}) {
                const auto& binding=expected.actors[scene][i].kinematic_admission.islands[slot].storage_header;
                auto& current=actual.kinematic_admission.islands[slot].storage_header;
                std::memcpy(current.data()+offset,binding.data()+offset,8);
                std::memcpy(current.data()+offset+12,binding.data()+offset+12,4);
                if(expected.node_domains[scene].valid)std::memcpy(current.data()+offset+8,binding.data()+offset+8,4);
            }
            if(actual.kinematic_admission.valid)for(unsigned slot=0;slot<2;++slot)for(unsigned list=0;list<2;++list) {
                const auto& binding=expected.actors[scene][i].kinematic_admission.islands[slot].list_headers[list];
                auto& current=actual.kinematic_admission.islands[slot].list_headers[list];
                std::memcpy(current.data(),binding.data(),8);std::memcpy(current.data()+12,binding.data()+12,4);
            }
            NormalizeInteractionStorage(actual, expected.actors[scene][i]);
            if (actual != expected.actors[scene][i]) return Status::failure(FailureCode::RestoreVerificationFailed);
        }
    return Status::success();
}

Status Sc6ReplayWorldState::InstallPhysicsProjection(const PhysicsBoundary& source, const PhysicsBoundary& destination, bool owned_render_work) noexcept
{
    const auto status = PreparePhysicsProjection(destination, source, nullptr, owned_render_work);
    if (!status.ok()) return status;
    __try {
        // All ranges and memberships were observed while the enclosing
        // application owns the hold. Validate storage before the first write.
        for (unsigned scene = 0; scene < destination.scenes.size(); ++scene)
        {
            if(source.node_domains[scene].island_ids_valid
                && (!PhysicsNodeLiveMatches(source.node_domains[scene],source.node_domains[scene])
                    || !PhysicsOrderLiveMatches(source,scene)))return Status::failure(FailureCode::GenerationMismatch);
            if(source.notifications[scene].valid && (!PhysicsNotificationsLiveMatch(source,scene)
                || !destination.notifications[scene].CanWrite(PhysicsNotificationRead{},[](std::uintptr_t p,std::size_t n) {
                    return Writable(reinterpret_cast<void*>(p),n);
                })))return Status::failure(FailureCode::GenerationMismatch);
            if(PhysicsOrderDiffers(source,destination,scene)
                && (!PhysicsOrderWritable(source,scene,destination)
                    || (source.node_domains[scene].valid && !PhysicsNodeAllocationWritable(source.node_domains[scene],destination.node_domains[scene]))))
                return Status::failure(FailureCode::RestorePreflightFailed);
            for (unsigned i = 0; i < destination.observed_actors[scene]; ++i) {
                const auto& a = destination.actors[scene][i];
                if (!a.mutable_projection()) continue;
                if(!source.notifications[scene].valid && IsolatedPhysicsKinematic(destination,scene,i) && IsolatedPhysicsKinematic(source,scene,i)) {
                    const auto* sc=reinterpret_cast<void*>(ReadAt<std::uintptr_t>(a.simulation_storage.data(),0x40));
                    if(!sc || ReadAt<unsigned>(sc,0x1080+0x34) || ReadAt<unsigned>(sc,0x10b8+0x34))
                        return Status::failure(FailureCode::GenerationMismatch);
                }
                if (!Writable(reinterpret_cast<void*>(a.actor), a.dynamic_storage.size())
                    || !Writable(reinterpret_cast<void*>(a.kinematic), a.kinematic_storage.size())
                    || !Writable(reinterpret_cast<void*>(a.simulation + 0xb4), 2)
                    || !Writable(reinterpret_cast<void*>(a.simulation + 0x60),28)
                    || !Writable(reinterpret_cast<void*>(a.component + 0x240),4)
                    || !Writable(reinterpret_cast<void*>(a.component + 0x24c), 0x30c - 0x24c))
                    return Status::failure(FailureCode::RestorePreflightFailed);
            }
        }
        for (unsigned scene = 0; scene < destination.scenes.size(); ++scene) {
            if (!destination.scenes[scene]) continue;
            auto* native_scene = reinterpret_cast<void*>(destination.scenes[scene]);
            const auto* table = ReadAt<const std::uintptr_t*>(native_scene, 0);
            if (table[0x330 / 8] != destination.module + 0x431d0 || table[0x338 / 8] != destination.module + 0x432b0)
                return Status::failure(FailureCode::IdentityMismatch);
            reinterpret_cast<void (*)(void*, const char*, unsigned)>(table[0x330 / 8])(native_scene, __FILE__, __LINE__);
            __try {
                if(PhysicsOrderDiffers(source,destination,scene)) {
                    const auto& source_nodes=source.node_domains[scene];const auto& target_nodes=destination.node_domains[scene];
                    if(!PhysicsOrderLiveMatches(source,scene) || !PhysicsNodeLiveMatches(source_nodes,source_nodes))return Status::failure(FailureCode::GenerationMismatch);
                    if(!WritePhysicsOrder(destination,scene,&source)
                        || (source_nodes.valid && !WritePhysicsNodeAllocation(target_nodes,source_nodes))
                        || !PhysicsOrderLiveMatches(destination,scene,&source) || !PhysicsNodeLiveMatches(target_nodes,source_nodes)) {
                        const bool recovered=WritePhysicsOrder(source,scene)
                            && (!source_nodes.valid || WritePhysicsNodeAllocation(source_nodes,source_nodes))
                            && PhysicsOrderLiveMatches(source,scene) && PhysicsNodeLiveMatches(source_nodes,source_nodes);
                        return Status::failure(recovered?FailureCode::RestoreVerificationFailed:FailureCode::UndoFailed);
                    }
                }
                if(destination.notifications[scene].valid
                    && !destination.notifications[scene].Write(PhysicsNotificationRead{},PhysicsNotificationWrite{}))
                    return Status::failure(FailureCode::RestoreVerificationFailed);
                for (unsigned i = 0; i < destination.observed_actors[scene]; ++i) {
                    const auto& a = destination.actors[scene][i];
                    if (!a.mutable_projection()) continue;
                    for (const auto span : {std::pair{0x90u, 28u}, {0xd0u, 12u}, {0xe0u, 12u}, {0x140u, 52u}})
                        std::memcpy(reinterpret_cast<void*>(a.actor + span.first), a.dynamic_storage.data() + span.first, span.second);
                    // Moved/countdown and notification bits have independent
                    // scheduling/queue proofs; all other flags remain exact.
                    std::memcpy(reinterpret_cast<void*>(a.simulation + 0xb4), a.simulation_storage.data() + 0xb4, 2);
                    if(IsolatedPhysicsKinematic(destination,scene,i) && IsolatedPhysicsKinematic(source,scene,i)) {
                        std::memcpy(reinterpret_cast<void*>(a.actor+0x11c),a.dynamic_storage.data()+0x11c,4);
                        std::memcpy(reinterpret_cast<void*>(a.actor+0x174),a.dynamic_storage.data()+0x174,8);
                    }
                    std::memcpy(reinterpret_cast<void*>(a.simulation + 0x60),a.simulation_storage.data()+0x60,28);
                    std::memcpy(reinterpret_cast<void*>(a.kinematic), a.kinematic_storage.data(), 29);
                    std::memcpy(reinterpret_cast<void*>(a.component + 0x270), a.component_transform.data(), 48);
                    unsigned cursor{};
                    for (const auto span : {std::pair{0x24cu, 28u}, {0x2a0u, 28u}, {0x2c0u, 24u}, {0x2e0u, 28u}, {0x300u, 12u}}) {
                        std::memcpy(reinterpret_cast<void*>(a.component + span.first), a.component_transform_auxiliary.data() + cursor, span.second);
                        cursor += span.second;
                    }
                    auto* flags=reinterpret_cast<unsigned*>(a.component+0x240);
                    *flags=(*flags&~1u)|(a.component_transform_flags&1u);
                }
            }
            __finally { reinterpret_cast<void (*)(void*)>(table[0x338 / 8])(native_scene); }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreVerificationFailed); }
}

Status Sc6ReplayWorldState::ReconstructPhysicsQueries(const PhysicsBoundary& expected) noexcept
{
    if (!expected.valid_counts()) return Status::failure(FailureCode::CapacityExceeded);
    __try {
        for (unsigned scene = 0; scene < expected.scenes.size(); ++scene) {
            if (!expected.scenes[scene]) continue;
            auto* native_scene = reinterpret_cast<void*>(expected.scenes[scene]);
            const auto* table = ReadAt<const std::uintptr_t*>(native_scene, 0);
            if (table[0x330 / 8] != expected.module + 0x431d0 || table[0x338 / 8] != expected.module + 0x432b0)
                return Status::failure(FailureCode::IdentityMismatch);
            reinterpret_cast<void (*)(void*, const char*, unsigned)>(table[0x330 / 8])(native_scene, __FILE__, __LINE__);
            __try {
                auto* manager = static_cast<std::byte*>(native_scene) + 0x2450;
                const auto capacity = ReadAt<unsigned>(manager, 0x54) & 0x7fffffff;
                unsigned pending{};
                for (unsigned i = 0; i < expected.observed_actors[scene]; ++i) {
                    const auto& actor = expected.actors[scene][i];
                    if (!actor.mutable_projection()) continue;
                    for (unsigned h = 0; h < actor.query_handle_count; ++h) {
                        const auto handle = actor.query_handles[h];
                        if (handle == 0xffffffff) continue;
                        if (!capacity) return Status::failure(FailureCode::CapacityExceeded);
                        // CE5E0 is the per-handle callee used by 571A0. CDEC0
                        // appends only if not already dirty; never exceed the
                        // existing list capacity or invoke its growth path.
                        reinterpret_cast<void (*)(void*, std::uint64_t)>(expected.module + 0xce5e0)(manager, handle);
                        if (++pending == capacity) {
                            reinterpret_cast<void (*)(void*)>(expected.module + 0xce670)(manager);
                            pending = 0;
                        }
                    }
                }
                // CE670 owns the query mutex, recomputes bounds through D5850
                // from restored body/kinematic poses, then commits both pruners.
                reinterpret_cast<void (*)(void*)>(expected.module + 0xce670)(manager);
            }
            __finally { reinterpret_cast<void (*)(void*)>(table[0x338 / 8])(native_scene); }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::RestoreVerificationFailed); }
}

void Sc6ReplayWorldState::ClearRecords() noexcept
{
    // 14216CDF0 receives the delegate subobject, not the timer record.
    for (std::size_t i = 0; i < count_; ++i)
        reinterpret_cast<void (*)(void*)>(base_ + 0x216cdf0)(records_[i].bytes.data() + 0x10);
    count_ = callback_bytes_ = 0;
    valid_ = false;
}

// ALuxBattleCamera registration 1408E1DA0, constructor 1403AB720 and
// manager assignment 1403E7BD7 establish the owner; old LuxAnim names were wrong.
// 1403B31F0 owns distance smoothing, 1403B35B0 the six-word finite envelope,
// and 1403D2A30 publishes the five DOF fields and override masks below.
Status Sc6ReplayWorldState::ReadCamera(std::uintptr_t base, void* world,
    CameraBinding& binding, Values& values) noexcept
{
    __try {
        binding = {};
        auto* battle = reinterpret_cast<void* (*)(void*)>(base + 0x3ef7a0)(world);
        auto* camera = battle ? ReadAt<void*>(battle, 0x3a8) : nullptr;
        auto* component = camera ? ReadAt<void*>(camera, 0x390) : nullptr;
        if (!battle || !camera || !component || ReadAt<void*>(camera, 0x98) != battle
            || ReadAt<std::uintptr_t>(camera, 0) != base + 0x3269248)
            return Status::failure(FailureCode::GenerationMismatch);
        // Reflected property registration14094F000: BattleTimeManager+4F8.
        // 140437590 owns scales+400, byte inhibits+410 and clocks+420/+424.
        auto* time = ReadAt<void*>(battle,0x4f8);
        auto* collection = ReadAt<void*>(battle,0x1420);
        void* instance{};
        const auto collection_count = ReadAt<int>(world,0x150);
        if(collection_count<0 || collection_count>128 || !time || !collection
            || ReadAt<void*>(time,0x98)!=battle) return Status::failure(FailureCode::GenerationMismatch);
        auto** collections = ReadAt<void**>(world,0x148);
        for(int i=0;i<collection_count;++i) {
            auto* candidate=collections[i];
            if(candidate && ReadAt<void*>(candidate,0x30)==collection) {
                if(instance) return Status::failure(FailureCode::GenerationMismatch);
                instance=candidate;
            }
        }
        if(!instance || ReadAt<void*>(instance,0x38)!=world) return Status::failure(FailureCode::GenerationMismatch);
        binding.objects = {battle, camera, component, time, collection, instance};
        for (std::size_t i=0; i<binding.objects.size(); ++i) {
            reinterpret_cast<void (*)(void*,const void*)>(base+0xf7bad0)(binding.weak[i].data(),binding.objects[i]);
            if (reinterpret_cast<void* (*)(const void*)>(base+0xf823f0)(binding.weak[i].data()) != binding.objects[i])
                return Status::failure(FailureCode::GenerationMismatch);
        }
        binding.time_vtable=ReadAt<std::uintptr_t>(time,0);
        std::memcpy(binding.time_arrays.data(),static_cast<std::byte*>(time)+0x400,0x20);
        const auto scales=ReadAt<const void*>(time,0x400), inhibits=ReadAt<const void*>(time,0x410);
        const int scale_count=ReadAt<int>(time,0x408), inhibit_count=ReadAt<int>(time,0x418);
        if(scale_count<0 || scale_count>16 || inhibit_count<0 || inhibit_count>16
            || ReadAt<int>(time,0x40c)<scale_count || ReadAt<int>(time,0x41c)<inhibit_count
            || (scale_count && !scales) || (inhibit_count && !inhibits)) return Status::failure(FailureCode::CapturePreflightFailed);
        if(scale_count) std::memcpy(values.time_scales.data(),scales,std::size_t(scale_count)*4);
        if(inhibit_count) std::memcpy(values.time_inhibit.data(),inhibits,std::size_t(inhibit_count));
        std::memcpy(values.material_clocks.data(),static_cast<std::byte*>(time)+0x420,8);
        binding.collection_resource=ReadAt<std::uintptr_t>(instance,0xe0);
        binding.authored_scalars=ReadAt<std::uintptr_t>(collection,0x38);
        binding.scalar_count=ReadAt<int>(collection,0x40);
        // This authored collection is scalar-only. A vector-bearing asset is
        // an explicit uncovered case, never silently omitted from its image.
        if(!binding.collection_resource || binding.scalar_count<1 || binding.scalar_count>32
            || !binding.authored_scalars || ReadAt<int>(collection,0x50)!=0
            || ReadAt<int>(instance,0x98)!=ReadAt<int>(instance,0xc4)) return Status::failure(FailureCode::CapturePreflightFailed);
        std::memcpy(binding.collection_maps.data(),static_cast<std::byte*>(instance)+0x40,0xa0);
        auto* scalar_map=static_cast<std::byte*>(instance)+0x40;
        const int entries=ReadAt<int>(scalar_map,8), free=ReadAt<int>(scalar_map,0x34);
        const auto data=ReadAt<std::uintptr_t>(scalar_map,0), heap=ReadAt<std::uintptr_t>(scalar_map,0x20);
        const auto flags=heap?heap:reinterpret_cast<std::uintptr_t>(scalar_map)+0x10;
        if(entries<0 || entries>32 || free<0 || free>entries || ReadAt<int>(scalar_map,0xc)<entries
            || ReadAt<int>(scalar_map,0x28)!=entries || ReadAt<int>(scalar_map,0x2c)<entries
            || (!heap && ReadAt<int>(scalar_map,0x2c)>128) || (entries && !data)) return Status::failure(FailureCode::CapturePreflightFailed);
        unsigned active{};
        for(int i=0;i<binding.scalar_count;++i) {
            const auto row=reinterpret_cast<const void*>(binding.authored_scalars+std::size_t(i)*0x20);
            binding.scalar_names[i]=ReadAt<std::uint64_t>(row,0);
            binding.scalar_defaults[i]=ReadAt<std::uint32_t>(row,0x18);
            values.material_scalars[i]=binding.scalar_defaults[i];
            for(int j=0;j<entries;++j) {
                if(!(ReadAt<unsigned>(reinterpret_cast<void*>(flags),std::size_t(j/32)*4)&(1u<<(j%32)))) continue;
                const auto entry=reinterpret_cast<const void*>(data+std::size_t(j)*0x18);
                if(ReadAt<std::uint64_t>(entry,0)!=binding.scalar_names[i]) continue;
                if(binding.scalar_values[i]) return Status::failure(FailureCode::CapturePreflightFailed);
                binding.scalar_values[i]=reinterpret_cast<std::uintptr_t>(entry)+8;
                values.material_scalars[i]=ReadAt<std::uint32_t>(entry,8);++active;
            }
        }
        if(active!=unsigned(entries-free)) return Status::failure(FailureCode::CapturePreflightFailed);
        binding.component_vtable = ReadAt<std::uintptr_t>(component,0);
        binding.provider = reinterpret_cast<std::uintptr_t (*)(void*)>(base+0x5070c0)(camera);
        if (!binding.provider) return Status::failure(FailureCode::ContextUnavailable);
        std::memcpy(binding.authored_dof.data(),reinterpret_cast<void*>(binding.provider+0x250),0x60);
        values.camera_distance = ReadAt<std::uint32_t>(camera,0xa88);
        values.camera_cursor = ReadAt<std::uint32_t>(camera,0x95c);
        values.camera_mode = ReadAt<std::uint8_t>(camera,0xa8c);
        std::memcpy(values.camera_envelope.data(),static_cast<std::byte*>(camera)+0x9c8,24);
        constexpr std::array<std::size_t,5> lens{0x838,0x844,0x848,0x858,0x85c};
        for(std::size_t i=0;i<lens.size();++i) values.camera_lens[i]=ReadAt<std::uint32_t>(component,lens[i]);
        values.camera_override_bits={ReadAt<std::uint32_t>(component,0x40c)&0x08000000,
            ReadAt<std::uint32_t>(component,0x410)&0xe3};
        values.camera_dof_method=ReadAt<std::uint8_t>(component,0x828);
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}
bool Sc6ReplayWorldState::ValidateCamera(std::uintptr_t base, void* world, const CameraBinding& binding,BindingDiagnostic* diagnostic) noexcept
{
    __try {
        // Resolve saved weak identities BEFORE following saved object pointers.
        for(std::size_t i=0;i<binding.objects.size();++i)
            if(!binding.objects[i] || reinterpret_cast<void* (*)(const void*)>(base+0xf823f0)(binding.weak[i].data())!=binding.objects[i]) return false;
        CameraBinding observed{}; Values ignored{};
        const auto read=ReadCamera(base,world,observed,ignored);
        if(!read.ok()) {if(diagnostic)*diagnostic={"camera_read",0,0,0,0,static_cast<unsigned>(read.code)};return false;}
        if(observed!=binding) {
            if(BindingDifference(binding.objects,observed.objects,"camera_objects",diagnostic))return false;
            if(BindingDifference(binding.weak,observed.weak,"camera_weak",diagnostic))return false;
            if(BindingDifference(binding.component_vtable,observed.component_vtable,"camera_component_vtable",diagnostic))return false;
            if(BindingDifference(binding.provider,observed.provider,"camera_provider",diagnostic))return false;
            if(BindingDifference(binding.time_arrays,observed.time_arrays,"camera_time_arrays",diagnostic))return false;
            if(BindingDifference(binding.collection_maps,observed.collection_maps,"camera_collection_maps",diagnostic))return false;
            if(BindingDifference(binding.scalar_names,observed.scalar_names,"camera_scalar_names",diagnostic))return false;
            if(BindingDifference(binding.scalar_values,observed.scalar_values,"camera_scalar_values",diagnostic))return false;
            if(BindingDifference(binding.scalar_defaults,observed.scalar_defaults,"camera_scalar_defaults",diagnostic))return false;
            if(BindingDifference(binding.time_vtable,observed.time_vtable,"camera_time_vtable",diagnostic))return false;
            if(BindingDifference(binding.collection_resource,observed.collection_resource,"camera_collection_resource",diagnostic))return false;
            if(BindingDifference(binding.authored_scalars,observed.authored_scalars,"camera_authored_scalars",diagnostic))return false;
            if(BindingDifference(binding.scalar_count,observed.scalar_count,"camera_scalar_count",diagnostic))return false;
            if(BindingDifference(binding.authored_dof,observed.authored_dof,"camera_authored_dof",diagnostic))return false;
            return false;
        }
        auto* camera=static_cast<std::byte*>(binding.objects[1]);
        auto* component=static_cast<std::byte*>(binding.objects[2]);
        auto* time=static_cast<std::byte*>(binding.objects[3]);
        if(!Writable(time+0x420,8)
            || !Writable(ReadAt<void*>(time,0x400),std::size_t(ReadAt<int>(time,0x408))*4)
            || !Writable(ReadAt<void*>(time,0x410),std::size_t(ReadAt<int>(time,0x418)))) return false;
        for(int i=0;i<binding.scalar_count;++i)
            if(binding.scalar_values[i] && !Writable(reinterpret_cast<void*>(binding.scalar_values[i]),4)) return false;
        return Writable(camera+0xa88,5) && Writable(camera+0x95c,4) && Writable(camera+0x9c8,24)
            && Writable(component+0x40c,8) && Writable(component+0x828,1) && Writable(component+0x838,0x28);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Sc6ReplayWorldState::WriteCamera(const CameraBinding& binding, const Values& values) noexcept
{
    __try {
        auto* camera=static_cast<std::byte*>(binding.objects[1]);
        auto* component=static_cast<std::byte*>(binding.objects[2]);
        auto* time=static_cast<std::byte*>(binding.objects[3]);
        const auto scale_count=ReadAt<int>(time,0x408),inhibit_count=ReadAt<int>(time,0x418);
        if(scale_count) std::memcpy(ReadAt<void*>(time,0x400),values.time_scales.data(),std::size_t(scale_count)*4);
        if(inhibit_count) std::memcpy(ReadAt<void*>(time,0x410),values.time_inhibit.data(),std::size_t(inhibit_count));
        std::memcpy(time+0x420,values.material_clocks.data(),8);
        for(int i=0;i<binding.scalar_count;++i)
            if(binding.scalar_values[i]) std::memcpy(reinterpret_cast<void*>(binding.scalar_values[i]),&values.material_scalars[i],4);
        // No native setter/queued publication here. The enclosing RT image
        // owns MPC uniform/scene-map publication and complete B undo.
        std::memcpy(camera+0xa88,&values.camera_distance,4);
        std::memcpy(camera+0x95c,&values.camera_cursor,4);
        std::memcpy(camera+0xa8c,&values.camera_mode,1);
        std::memcpy(camera+0x9c8,values.camera_envelope.data(),24);
        constexpr std::array<std::size_t,5> lens{0x838,0x844,0x848,0x858,0x85c};
        for(std::size_t i=0;i<lens.size();++i) std::memcpy(component+lens[i],&values.camera_lens[i],4);
        auto primary=(ReadAt<std::uint32_t>(component,0x40c)&~0x08000000u)|values.camera_override_bits[0];
        auto secondary=(ReadAt<std::uint32_t>(component,0x410)&~0xe3u)|values.camera_override_bits[1];
        std::memcpy(component+0x40c,&primary,4); std::memcpy(component+0x410,&secondary,4);
        std::memcpy(component+0x828,&values.camera_dof_method,1);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayWorldState::Read(std::uintptr_t base, void* world, Values& values,
    void*& manager, void*& owner, std::array<Header, 3>& arrays, CameraBinding& camera) noexcept
{
    __try
    {
        const auto camera_status=ReadCamera(base,world,camera,values);
        if(!camera_status.ok()) return camera_status;
        // Resolver 141EF4C40 selects the instance-owned manager when present.
        auto* instance = ReadAt<void*>(world, 0x140);
        manager = instance ? ReadAt<void*>(instance, 0xd8) : ReadAt<void*>(world, 0x430);
        if (!manager || ReadAt<std::uintptr_t>(manager, 0) != base + 0x39df498)
            return Status::failure(FailureCode::IdentityMismatch);
        owner = ReadAt<void*>(manager, 0x118);
        std::memcpy(values.world_clocks.data(), static_cast<std::byte*>(world) + 0x930, 20);
        values.timer_clock = ReadAt<std::uint64_t>(manager, 0x40);
        values.ue_random_state = ReadAt<std::uint32_t>(reinterpret_cast<void*>(base),0x416673c);
        std::memcpy(values.foot_world_history.data(),reinterpret_cast<void*>(base+0x470e890),128);
        std::memcpy(values.foot_height.data(),reinterpret_cast<void*>(base+0x470e910),16);
        std::memcpy(values.foot_phase.data(),reinterpret_cast<void*>(base+0x470e920),16);
        std::memcpy(values.foot_material.data(),reinterpret_cast<void*>(base+0x470e930),16);
        std::memcpy(values.foot_pose_scalars.data(),reinterpret_cast<void*>(base+0x470e948),16);
        std::memcpy(values.foot_vfx_suppressed.data(),reinterpret_cast<void*>(base+0x470e970),2);
        std::memcpy(values.foot_vfx_scale.data(),reinterpret_cast<void*>(base+0x470e99c),8);
        std::memcpy(values.weapon_contact_frames.data(),reinterpret_cast<void*>(base+0x470e984),16);
        values.timers_admitted = ReadAt<std::uint64_t>(manager, 0x110)
            == ReadAt<std::uint64_t>(reinterpret_cast<void*>(base), 0x4197170);
        // No capture while a callback owns the embedded executing record.
        if (ReadAt<std::uint64_t>(manager, 0x100) != 0)
            return Status::failure(FailureCode::IllegalTransition);
        for (std::size_t i = 0; i < arrays.size(); ++i)
        {
            arrays[i] = ReadAt<Header>(manager, 0x10 + i * 0x10);
            const auto& array = arrays[i];
            if (array.count < 0 || array.capacity < array.count || (array.count && !array.data))
                return Status::failure(FailureCode::CapturePreflightFailed);
            values.counts[i] = array.count;
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::ValidateRecord(std::uintptr_t base, const Record& record,
    std::uint8_t state, std::size_t& callback_bytes) noexcept
{
    __try
    {
        const auto* p = record.bytes.data();
        if (ReadAt<std::uint8_t>(p, 1) != state)
            return Status::failure(FailureCode::CapturePreflightFailed);
        // Function captures need their own concrete clone/lifetime audit.
        if (ReadAt<std::int32_t>(p, 0xa0) || ReadAt<void*>(p, 0x90))
            return Status::failure(FailureCode::UnsupportedContent);
        // Native FWeakObjectPtr_Get checks index, serial and pending-kill /
        // unreachable flags. Equal weak-pointer bytes alone cannot detect an
        // owner destroyed after capture. Empty script delegates have serial 0.
        // A stale weak target can already be present in the native timer heap.
        // 1421762F0 tests +38 IsSafeToExecute before invoking +60, and
        // 14217EE10 tests binding again before looping reinsertion. Preserve
        // the inert record; the enclosing binding fingerprint rejects any
        // live/dead change after capture, including during publication/undo.
        const auto units = ReadAt<std::int32_t>(p, 0x40);
        if (units == 0) return Status::success();
        const auto* delegate = NativeDelegate(p);
        const auto vtable = ReadAt<std::uintptr_t>(delegate, 0);
        if (units != 3 || !ReadAt<void*>(p, 0x30)
            || (vtable != base + 0x373f4a0 && vtable != base + 0x37af460))
            return Status::failure(FailureCode::UnsupportedContent);
        // Both audited clone implementations allocate three 16-byte units.
        const auto bytes = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base + 0xd50dc0)(48, 0);
        if (bytes < 48 || bytes > std::numeric_limits<std::size_t>::max() - callback_bytes)
            return Status::failure(FailureCode::CapacityExceeded);
        callback_bytes += bytes;
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

bool Sc6ReplayWorldState::SameRecord(std::uintptr_t base, const Record& first, const Record& second) noexcept
{
    __try
    {
        const auto* a = first.bytes.data();
        const auto* b = second.bytes.data();
        if ((ReadAt<unsigned char>(a, 0) & 3) != (ReadAt<unsigned char>(b, 0) & 3)
            || ReadAt<unsigned char>(a, 1) != ReadAt<unsigned char>(b, 1)
            || std::memcmp(a + 4, b + 4, 12) || std::memcmp(a + 0xb0, b + 0xb0, 12)
            || std::memcmp(a + 0x50, b + 0x50, 16)
            || ReadAt<int>(a, 0x40) != ReadAt<int>(b, 0x40)) return false;
        if (!ReadAt<int>(a, 0x40)) return true;
        const auto* da = NativeDelegate(a);
        const auto* db = NativeDelegate(b);
        // Clone copies omit padding at +0x18 (40-byte type) or +0x20 (48-byte type).
        const auto kind = ReadAt<std::uintptr_t>(da, 0);
        if (kind != ReadAt<std::uintptr_t>(db, 0)
            || ReadAt<std::uintptr_t>(a, 0x30) == ReadAt<std::uintptr_t>(b, 0x30)) return false;
        // Copies must not alias the live callback allocation.
        // Both kinds retain the weak owner and method pointer; the larger
        // member-pointer representation additionally copies +0x18.
        if (kind == base + 0x373f4a0)
            return std::memcmp(da, db, 24) == 0
                && ReadAt<std::uint64_t>(da, 0x20) == ReadAt<std::uint64_t>(db, 0x20);
        if (kind == base + 0x37af460)
            return std::memcmp(da, db, 32) == 0
                && ReadAt<std::uint64_t>(da, 0x28) == ReadAt<std::uint64_t>(db, 0x28);
        return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayWorldState::FingerprintCallbackBindings(std::uintptr_t base, const Record* records,
    std::size_t count, std::uint64_t& output) noexcept
{
    __try {
        output = 14695981039346656037ull;
        const auto resolve = reinterpret_cast<void* (*)(const void*)>(base + 0xf823f0);
        for (std::size_t i = 0; i < count; ++i) {
            const auto* p = records[i].bytes.data();
            const auto script = reinterpret_cast<std::uintptr_t>(resolve(p + 0x50));
            const auto native = ReadAt<int>(p, 0x40)
                ? reinterpret_cast<std::uintptr_t>(resolve(static_cast<const std::byte*>(NativeDelegate(p)) + 8)) : 0;
            for (const auto value : {ReadAt<std::uint64_t>(p, 0xb0), script, native}) {
                output ^= value; output *= 1099511628211ull;
            }
        }
        return Status::success();
    } __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::ReadStage(std::uintptr_t base,std::uintptr_t actor,ReplayStageVisibility& output) noexcept
{
    __try {
        const auto read=[](std::uintptr_t p,auto& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
        const auto weak=[&](std::uintptr_t p,auto& pair){
            reinterpret_cast<void(*)(void*,const void*)>(base+0xf7bad0)(pair.data(),reinterpret_cast<void*>(p));
            return reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base+0xf823f0)(pair.data()))==p;
        };
        return output.ReadFrom(base,actor,read,weak)?Status::success():Status::failure(FailureCode::UnsupportedContent);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayWorldState::ValidateStage(std::uintptr_t base,std::span<const ReplayStageVisibility> image,bool values,BindingDiagnostic* diagnostic,const Sc6ReplaySchedulerState* scheduler,
    const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore) noexcept
{
    __try {
        for(const auto& saved:image) {
            if(reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base+0xf823f0)(saved.weak.data()))!=saved.actor)
                return Status::failure(FailureCode::GenerationMismatch);
            ReplayStageVisibility current{};
            auto status=ReadStage(base,saved.actor,current);
            if(!status.ok()) {if(diagnostic)*diagnostic={"stage_read",saved.actor,0,0,0,static_cast<unsigned>(status.code)};return status;}
            unsigned owned_registration{};
            if(scheduler)for(unsigned i=0;i<saved.component_count;++i)
                if(scheduler->OwnsStageComponent(saved.components[i]))owned_registration|=1u<<i;
            if(!saved.SameBinding(current,owned_registration) || (values && saved.values!=current.values)) {
                if(diagnostic) {
                    *diagnostic={"stage_binding",saved.actor,0,0,static_cast<unsigned>(saved.values.break_state),static_cast<unsigned>(current.values.break_state)};
                    for(unsigned i=0;i<saved.component_count && i<current.component_count;++i) {
                        auto a=saved.components[i],b=current.components[i];
                        a.visibility&=~0x10u;b.visibility&=~0x10u;
                        a.flags&=~0xc0000060u;b.flags&=~0xc0000060u;
                        if(a==b)continue;

                        if(BindingDifference(a.object,b.object,"stage_object",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.type,b.type,"stage_type",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.parent,b.parent,"stage_parent",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.asset,b.asset,"stage_asset",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.weak,b.weak,"stage_weak",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.visibility,b.visibility,"stage_visibility",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.flags,b.flags,"stage_flags",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.primitive_id,b.primitive_id,"stage_primitive_id",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.member_offset,b.member_offset,"stage_member_offset",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.asset_offset,b.asset_offset,"stage_asset_offset",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.materials,b.materials,"stage_materials",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.children,b.children,"stage_children",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.mids,b.mids,"stage_mids",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.child_objects,b.child_objects,"stage_child_objects",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.mid_weak,b.mid_weak,"stage_mid_weak",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.material_count,b.material_count,"stage_material_count",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.material_capacity,b.material_capacity,"stage_material_capacity",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.child_count,b.child_count,"stage_child_count",diagnostic,saved.actor,i))break;
                        if(BindingDifference(a.child_capacity,b.child_capacity,"stage_child_capacity",diagnostic,saved.actor,i))break;
                        break;
                    }
                }
                if(diagnostic && diagnostic->component<current.component_count) {
                    const auto& c=current.components[diagnostic->component];
                    diagnostic->registration_entry=c.registration_entry-base;
                    diagnostic->primary_tick_flags=c.primary_tick_flags;
                    diagnostic->base_registration=c.base_registration;
                    diagnostic->secondary_tick_flags=c.secondary_tick_flags;
                    if(diagnostic->component<saved.component_count) {
                        diagnostic->expected_primary_flags=saved.components[diagnostic->component].primary_tick_flags;
                        diagnostic->expected_secondary_flags=saved.components[diagnostic->component].secondary_tick_flags;
                    }
                }
                return Status::failure(FailureCode::GenerationMismatch);
            }
            if(values)for(unsigned i=0;i<saved.component_count;++i)
                if(((saved.components[i].flags^current.components[i].flags)&0x20000000u)
                    || ((saved.components[i].primary_tick_flags!=current.components[i].primary_tick_flags
                        || saved.components[i].secondary_tick_flags!=current.components[i].secondary_tick_flags)
                        && !(scheduler && scheduler_restore
                            && scheduler->RestoreStageRegistration(*scheduler_restore,saved.components[i],false).ok())))
                    return Status::failure(FailureCode::RestoreVerificationFailed);
            for(const auto& c:saved.components) if(c.object
                && reinterpret_cast<std::uintptr_t>(reinterpret_cast<void*(*)(const void*)>(base+0xf823f0)(c.weak.data()))!=c.object)
                return Status::failure(FailureCode::GenerationMismatch);
        }
        return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

bool Sc6ReplayWorldState::WriteStage(std::uintptr_t base,std::span<const ReplayStageVisibility> image,
    const Sc6ReplaySchedulerState* scheduler,const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore) noexcept
{
    __try {
        // Complete destination preflight precedes the first source write.
        for(const auto& row:image) {
            if(!row.SupportedValues() || !Writable(reinterpret_cast<void*>(row.actor+0x389),1))return false;
            if(row.kind==ReplayStageVisibility::Kind::Mesh && !Writable(reinterpret_cast<void*>(row.actor+0x3c8),8))return false;
            if(row.kind==ReplayStageVisibility::Kind::Wall && !Writable(reinterpret_cast<void*>(row.actor+0x468),12))return false;
        }
        for(const auto& row:image)for(const auto& c:row.components)if(c.object) {
            const auto flags=ReadAt<unsigned>(reinterpret_cast<void*>(c.object),0x188);
            if(!((flags^c.flags)&0x20000000u))continue;
            if(scheduler && scheduler_restore && scheduler->OwnsStageComponent(c)) {
                if(!scheduler->RestoreStageRegistration(*scheduler_restore,c,false).ok())return false;
                continue;
            }
            if(!c.DormantRegistration() || !(flags&1)
                || ReadAt<std::uint8_t>(reinterpret_cast<void*>(c.object),0x11c)!=c.primary_tick_flags
                || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(c.object),0)),0x2d8)!=base+0x1d58ac0
                || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(base),0x4392300))return false;
        }
        for(const auto& row:image)for(const auto& c:row.components)if(c.object) {
            const auto flags=ReadAt<unsigned>(reinterpret_cast<void*>(c.object),0x188);
            if(!((flags^c.flags)&0x20000000u))continue;
            if(scheduler && scheduler_restore && scheduler->OwnsStageComponent(c)) {
                if(!scheduler->RestoreStageRegistration(*scheduler_restore,c,true).ok())return false;
                continue;
            }
            std::array<std::byte,0x58> prefix{};
            std::memcpy(prefix.data(),reinterpret_cast<void*>(c.object+0x110),prefix.size());
            reinterpret_cast<void(*)(void*,bool)>(base+0x1d58a20)(reinterpret_cast<void*>(c.object),bool(c.flags&0x20000000u));
            if(ReadAt<unsigned>(reinterpret_cast<void*>(c.object),0x188)!=((flags&~0x20000000u)|(c.flags&0x20000000u))
                || std::memcmp(prefix.data(),reinterpret_cast<void*>(c.object+0x110),prefix.size())
                || ReadAt<std::uintptr_t>(reinterpret_cast<void*>(base),0x4392300))return false;
        }
        for(const auto& row:image) if(!row.WriteValues([](std::uintptr_t p,const auto& value){
            std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));return true;
        }))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

Status Sc6ReplayWorldState::Capture(std::uintptr_t base, void* world, std::size_t budget,
    std::span<const std::uintptr_t> stage_actors) noexcept
{
    Values values{};
    void* manager{};
    void* owner{};
    std::array<Header, 3> arrays{};
    CameraBinding camera{};
    auto status = Read(base, world, values, manager, owner, arrays, camera);
    if (!status.ok()) return status;
    valid_=false;
    if(stage_actors.size()>128 || stage_actors.size()>budget/sizeof(ReplayStageVisibility))
        return Status::failure(FailureCode::CapacityExceeded);
    if(stage_actors.size()>stage_.capacity() && stage_actors.size()+stage_.capacity()>budget/sizeof(ReplayStageVisibility))
        return Status::failure(FailureCode::CapacityExceeded);
    try {stage_.resize(stage_actors.size());} catch(...) {return Status::failure(FailureCode::CapacityExceeded);}
    if(stage_.capacity()>budget/sizeof(ReplayStageVisibility))return Status::failure(FailureCode::CapacityExceeded);
    budget-=stage_.capacity()*sizeof(ReplayStageVisibility);
    for(std::size_t i=0;i<stage_.size();++i) {
        for(std::size_t j=0;j<i;++j) if(stage_actors[i]==stage_actors[j])return Status::failure(FailureCode::GenerationMismatch);
        status=ReadStage(base,stage_actors[i],stage_[i]);if(!status.ok())return status;
    }
    std::size_t count = 0, callback_bytes = 0;
    constexpr std::array<std::uint8_t, 3> states{1, 2, 0};
    for (std::size_t i = 0; i < arrays.size(); ++i)
    {
        if (static_cast<std::size_t>(arrays[i].count) > budget / sizeof(Record) - count)
            return Status::failure(FailureCode::CapacityExceeded);
        count += arrays[i].count;
        for (int j = 0; j < arrays[i].count; ++j)
        {
            status = ValidateRecord(base, arrays[i].data[j], states[i], callback_bytes);
            if (!status.ok()) return status;
        }
    }
    const auto capacity = count > capacity_ ? count : capacity_;
    if (capacity > budget / sizeof(Record) || callback_bytes > budget - capacity * sizeof(Record))
        return Status::failure(FailureCode::CapacityExceeded);
    ClearRecords();
    if (count > capacity_)
    {
        records_.reset();
        capacity_ = 0;
        try { records_ = std::make_unique<Record[]>(count); }
        catch (...) { return Status::failure(FailureCode::CapacityExceeded); }
        capacity_ = count;
    }
    base_ = base;
    world_ = world;
    manager_ = manager;
    owner_ = owner;
    thread_ = GetCurrentThreadId();
    values_ = values;
    camera_binding_ = camera;
    for (const auto& array : arrays)
        for (int i = 0; i < array.count; ++i)
        {
            auto& destination = records_[count_];
            destination = {};
            reinterpret_cast<void* (*)(void*, const void*)>(base + 0x216c3b0)(&destination, &array.data[i]);
            ++count_;
            if (!SameRecord(base, destination, array.data[i]))
            { ClearRecords(); return Status::failure(FailureCode::CaptureFailed); }
        }
    callback_bytes_ = callback_bytes;
    status = FingerprintCallbackBindings(base, records_.get(), count_, callback_binding_fingerprint_);
    if (!status.ok()) { ClearRecords(); return status; }
    valid_ = true;
    return Status::success();
}

Status Sc6ReplayWorldState::ValidateHeld(std::uintptr_t base, void* world) const noexcept
{
    if (!valid_ || base != base_ || world != world_ || GetCurrentThreadId() != thread_)
        return Status::failure(FailureCode::GenerationMismatch);
    auto stage_status=ValidateStage(base,stage_,true);if(!stage_status.ok())return stage_status;
    std::uint64_t bindings{};
    if (!FingerprintCallbackBindings(base, records_.get(), count_, bindings).ok() || bindings != callback_binding_fingerprint_)
        return Status::failure(FailureCode::GenerationMismatch);
    Values values{};
    void* manager{};
    void* owner{};
    std::array<Header, 3> arrays{};
    CameraBinding camera{};
    auto status = Read(base, world, values, manager, owner, arrays, camera);
    if (!status.ok()) return status;
    if (values != values_ || manager != manager_ || owner != owner_ || camera != camera_binding_)
        return Status::failure(FailureCode::GenerationMismatch);
    std::size_t index = 0, callback_bytes = 0;
    constexpr std::array<std::uint8_t, 3> states{1, 2, 0};
    for (std::size_t a = 0; a < arrays.size(); ++a)
        for (int i = 0; i < arrays[a].count; ++i, ++index)
        {
            if (index >= count_) return Status::failure(FailureCode::GenerationMismatch);
            status = ValidateRecord(base, arrays[a].data[i], states[a], callback_bytes);
            if (!status.ok()) return status;
            if (!SameRecord(base, records_[index], arrays[a].data[i]))
                return Status::failure(FailureCode::GenerationMismatch);
        }
    return index == count_ ? Status::success() : Status::failure(FailureCode::GenerationMismatch);
}

bool Sc6ReplayWorldState::Write(std::uintptr_t base, void* world, void* manager, const Values& values,
    const std::array<Header, 3>& arrays, std::uint64_t timer_epoch, const CameraBinding& camera) noexcept
{
    __try
    {
        if(!ValidateCamera(base,world,camera) || !WriteCamera(camera,values)) return false;
        std::memcpy(static_cast<std::byte*>(manager) + 0x10, arrays.data(), sizeof(Header) * 3);
        std::memcpy(static_cast<std::byte*>(manager) + 0x40, &values.timer_clock, 8);
        std::memcpy(static_cast<std::byte*>(world) + 0x930, values.world_clocks.data(), 20);
        // Native 140D32140 consumes this shared UE stream; the held world/render
        // transaction validates its unchanged value along with the clocks before
        // publication. This is RNG state, not a scheduling/occlusion epoch.
        std::memcpy(reinterpret_cast<void*>(base+0x416673c), &values.ue_random_state, 4);
        // These scalar lanes are absent from HgCpu's native battle snapshot.
        // Values participates in A publication, observed-C settlement and exact
        // private-B undo; never seed it from comparison observations.
        std::memcpy(reinterpret_cast<void*>(base+0x470e890),values.foot_world_history.data(),128);
        std::memcpy(reinterpret_cast<void*>(base+0x470e910),values.foot_height.data(),16);
        std::memcpy(reinterpret_cast<void*>(base+0x470e920),values.foot_phase.data(),16);
        std::memcpy(reinterpret_cast<void*>(base+0x470e930),values.foot_material.data(),16);
        std::memcpy(reinterpret_cast<void*>(base+0x470e948),values.foot_pose_scalars.data(),16);
        std::memcpy(reinterpret_cast<void*>(base+0x470e970),values.foot_vfx_suppressed.data(),2);
        std::memcpy(reinterpret_cast<void*>(base+0x470e99c),values.foot_vfx_scale.data(),8);
        std::memcpy(reinterpret_cast<void*>(base+0x470e984),values.weapon_contact_frames.data(),16);
        std::memcpy(static_cast<std::byte*>(manager) + 0x110, &timer_epoch, 8);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

Status Sc6ReplayWorldState::Restore(std::uintptr_t base, void* world, std::size_t budget) const noexcept
{
    PreparedRestore prepared;
    const auto status = PrepareRestore(base, world, budget, prepared);
    return status.ok() ? prepared.Apply() : status;
}

Sc6ReplayWorldState::PreparedRestore::~PreparedRestore() { Clear(); }

void Sc6ReplayWorldState::PreparedRestore::Clear() noexcept
{
    // An uncertain publication may still point at either graph. The caller
    // must retain this participant and recover it before session teardown.
    if (published_ || render_published_) return;
    ClearRenderWork();
    if (base_)
        for (auto& array : arrays_)
            reinterpret_cast<void (*)(void*)>(base_ + 0x216c9e0)(&array);
    arrays_ = {};
    previous_arrays_ = {};
    owned_bytes_ = 0;
    base_ = 0;
    ready_ = false;
    stage_.clear();previous_stage_.clear();
    stage_scheduler_=nullptr;stage_scheduler_restore_=nullptr;
    write_complete_ = false;
    undo_started_ = false;
    executing_ = execution_settled_ = false;
    execution_retirement_budget_ = 0;
}

Status Sc6ReplayWorldState::PrepareRestore(std::uintptr_t base, void* world, std::size_t budget,
    PreparedRestore& prepared) const noexcept
{
    if (prepared.ready_ || prepared.published_) return Status::failure(FailureCode::IllegalTransition);
    if (!valid_ || base != base_ || world != world_ || GetCurrentThreadId() != thread_)
        return Status::failure(FailureCode::GenerationMismatch);
    std::uint64_t bindings{};
    if (!FingerprintCallbackBindings(base, records_.get(), count_, bindings).ok() || bindings != callback_binding_fingerprint_)
        return Status::failure(FailureCode::GenerationMismatch);
    Values before{};
    void* manager{};
    void* owner{};
    std::array<Header, 3> previous{};
    CameraBinding camera{};
    auto status = Read(base, world, before, manager, owner, previous, camera);
    if (!status.ok()) return status;
    if (manager != manager_ || owner != owner_ || camera != camera_binding_ || !ValidateCamera(base,world,camera_binding_))
        return Status::failure(FailureCode::GenerationMismatch);
    if (!Writable(static_cast<std::byte*>(manager) + 0x10, 0x38)
        || !Writable(static_cast<std::byte*>(manager) + 0x110, 8)
        || !Writable(static_cast<std::byte*>(world) + 0x930, 20))
        return Status::failure(FailureCode::RestorePreflightFailed);

    // Charge owned checkpoint storage, prepared native arrays and callbacks,
    // and the old arrays retained until commit verification. The caller still
    // needs to account for the rest of the replay's allocations.
    auto remaining = budget;
    const auto charge = [&remaining](std::size_t bytes) {
        if (bytes > remaining) return false;
        remaining -= bytes;
        return true;
    };
    if (!charge(owned_bytes())) return Status::failure(FailureCode::CapacityExceeded);
    constexpr std::array<std::uint8_t, 3> states{1, 2, 0};
    std::size_t index = 0, callback_bytes = 0, target_callbacks = 0, target_arrays = 0, retained_arrays = 0;
    for (std::size_t a = 0; a < previous.size(); ++a)
    {
        if (values_.counts[a] < 0 || static_cast<std::size_t>(values_.counts[a]) > count_ - index)
            return Status::failure(FailureCode::RestorePreflightFailed);
        for (int i = 0; i < previous[a].count; ++i)
        {
            status = ValidateRecord(base, previous[a].data[i], states[a], callback_bytes);
            if (!status.ok()) return status;
        }
        for (int i = 0; i < values_.counts[a]; ++i, ++index)
        {
            status = ValidateRecord(base, records_[index], states[a], target_callbacks);
            if (!status.ok()) return status;
        }
        for (int kind = 0; kind < 2; ++kind)
        {
            const auto capacity = kind ? values_.counts[a] : previous[a].capacity;
            if (!capacity) continue;
            const auto requested = static_cast<std::size_t>(capacity) * sizeof(Record);
            const auto bytes = reinterpret_cast<std::size_t (*)(std::size_t, unsigned)>(base + 0xd50dc0)(requested, 0);
            if (bytes < requested || !charge(bytes)) return Status::failure(FailureCode::CapacityExceeded);
            if (kind) target_arrays += bytes;
            else retained_arrays += bytes;
        }
    }
    if (index != count_) return Status::failure(FailureCode::RestorePreflightFailed);
    if (!charge(callback_bytes) || !charge(target_callbacks)) return Status::failure(FailureCode::CapacityExceeded);

    prepared.Clear();
    prepared.base_ = base;
    prepared.world_ = world;
    prepared.manager_ = manager;
    prepared.owner_ = owner;
    prepared.thread_ = thread_;
    if(stage_.size()>remaining/(2*sizeof(ReplayStageVisibility)))return Status::failure(FailureCode::CapacityExceeded);
    try {prepared.stage_=stage_;prepared.previous_stage_.resize(stage_.size());}
    catch(...) {prepared.Clear();return Status::failure(FailureCode::CapacityExceeded);}
    for(std::size_t i=0;i<stage_.size();++i) {
        status=ReadStage(base,stage_[i].actor,prepared.previous_stage_[i]);
        if(!status.ok() || !stage_[i].SameBinding(prepared.previous_stage_[i])) {
            prepared.Clear();return Status::failure(FailureCode::GenerationMismatch);
        }
    }
    prepared.values_ = values_;
    prepared.previous_values_ = before;
    prepared.camera_binding_ = camera_binding_;
    prepared.previous_arrays_ = previous;
    prepared.epoch_ = ReadAt<std::uint64_t>(reinterpret_cast<void*>(base), 0x4197170);
    prepared.previous_timer_epoch_ = ReadAt<std::uint64_t>(manager, 0x110);
    index = 0;
    for (std::size_t a = 0; a < prepared.arrays_.size(); ++a)
    {
        auto& array = prepared.arrays_[a];
        if (!values_.counts[a]) continue;
        auto* data = static_cast<Record*>(reinterpret_cast<void* (*)(std::size_t)>(base + 0x4a61c0)(
            static_cast<std::size_t>(values_.counts[a]) * sizeof(Record)));
        if (!data) { prepared.Clear(); return Status::failure(FailureCode::CapacityExceeded); }
        array.data = data;
        array.capacity = values_.counts[a];
        for (int i = 0; i < values_.counts[a]; ++i, ++index)
        {
            ::new (static_cast<void*>(&data[i])) Record{};
            reinterpret_cast<void* (*)(void*, const void*)>(base + 0x216c3b0)(&data[i], &records_[index]);
            ++array.count;
            if (!SameRecord(base, records_[index], data[i]))
            { prepared.Clear(); return Status::failure(FailureCode::RestorePreflightFailed); }
        }
    }
    // Reserve both graphs for the entire publication/undo window; B becomes
    // replay-retained storage when its native headers are displaced.
    prepared.owned_bytes_ = target_arrays + target_callbacks + retained_arrays + callback_bytes
        +(prepared.stage_.capacity()+prepared.previous_stage_.capacity())*sizeof(ReplayStageVisibility);
    prepared.ready_ = true;
    status = PreparedRestore::Fingerprint(base, prepared.arrays_, prepared.target_fingerprint_);
    if (status.ok()) status = PreparedRestore::Fingerprint(base, previous, prepared.previous_fingerprint_);
    if (status.ok()) status = prepared.ValidateImage(false);
    if (!status.ok()) prepared.Clear();
    return status;
}

Status Sc6ReplayWorldState::PreparedRestore::Fingerprint(std::uintptr_t base,
    const std::array<Header, 3>& arrays, std::uint64_t& output) noexcept
{
    __try
    {
        output = 14695981039346656037ull;
        const auto add = [&output](const void* data, std::size_t size) {
            const auto* bytes = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0; i < size; ++i) { output ^= bytes[i]; output *= 1099511628211ull; }
        };
        constexpr std::array<std::uint8_t, 3> states{1, 2, 0};
        std::size_t callback_bytes = 0;
        for (std::size_t a = 0; a < arrays.size(); ++a)
        {
            const auto& array = arrays[a];
            if (array.count < 0 || array.capacity < array.count || (array.capacity && !array.data))
                return Status::failure(FailureCode::RestorePreflightFailed);
            add(&array, sizeof(array));
            std::uint64_t bindings{};
            const auto binding_status = FingerprintCallbackBindings(base, array.data, array.count, bindings);
            if (!binding_status.ok()) return binding_status;
            add(&bindings, sizeof(bindings));
            for (int i = 0; i < array.count; ++i)
            {
                const auto& record = array.data[i];
                const auto status = ValidateRecord(base, record, states[a], callback_bytes);
                if (!status.ok()) return status;
                add(&record, sizeof(record));
                if (ReadAt<int>(&record, 0x40))
                {
                    const auto* delegate = NativeDelegate(&record);
                    const bool large = ReadAt<std::uintptr_t>(delegate, 0) == base + 0x37af460;
                    // Hash initialized callback fields, excluding clone padding.
                    add(delegate, large ? 32 : 24);
                    add(static_cast<const std::byte*>(delegate) + (large ? 0x28 : 0x20), 8);
                }
            }
        }
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

std::uint32_t Sc6ReplayWorldState::PreparedRestore::StageTickFlagMask(std::uintptr_t component) const noexcept
{
    __try {
        for(const auto& row:previous_stage_)for(const auto& c:row.components)
            if(c.object==component && stage_scheduler_ && stage_scheduler_->OwnsStageComponent(c))return 0x20000000u;
        for(const auto& row:previous_stage_)for(const auto& c:row.components)if(c.object==component && c.DormantRegistration()
            && (ReadAt<unsigned>(reinterpret_cast<void*>(component),0x188)&1)
            && ReadAt<std::uint8_t>(reinterpret_cast<void*>(component),0x11c)==c.primary_tick_flags
            && ReadAt<std::uintptr_t>(reinterpret_cast<void*>(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(component),0)),0x2d8)==base_+0x1d58ac0)
            return 0x20000000u;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateBinding() const noexcept
{
    return ValidateBinding(epoch_);
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateBinding(std::uint64_t epoch) const noexcept
{
    if (!ready_ || GetCurrentThreadId() != thread_) return Status::failure(FailureCode::IllegalTransition);
    __try
    {
        binding_diagnostic_={"stage_lifetime"};
        if(!ValidateStage(base_,previous_stage_,false,&binding_diagnostic_,stage_scheduler_).ok()) return Status::failure(FailureCode::GenerationMismatch);
        binding_diagnostic_={"camera_binding"};
        if(!ValidateCamera(base_,world_,camera_binding_,&binding_diagnostic_)) return Status::failure(FailureCode::GenerationMismatch);
        binding_diagnostic_={"timer_manager_binding"};
        auto* instance = ReadAt<void*>(world_, 0x140);
        auto* manager = instance ? ReadAt<void*>(instance, 0xd8) : ReadAt<void*>(world_, 0x430);
        if (manager != manager_ || !manager || ReadAt<std::uintptr_t>(manager, 0) != base_ + 0x39df498
            || ReadAt<void*>(manager, 0x118) != owner_ || ReadAt<std::uint64_t>(manager, 0x100)
            || ReadAt<std::uint64_t>(reinterpret_cast<void*>(base_), 0x4197170) != epoch)
            return Status::failure(FailureCode::GenerationMismatch);
        if (!Writable(static_cast<std::byte*>(manager) + 0x10, 0x38)
            || !Writable(static_cast<std::byte*>(manager) + 0x110, 8)
            || !Writable(static_cast<std::byte*>(world_) + 0x930, 20))
            return Status::failure(FailureCode::RestorePreflightFailed);
        return Status::success();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateImage(bool target) const noexcept
{
    auto status = ValidateBinding();
    if (!status.ok()) return status;
    Values values{};
    void* manager{};
    void* owner{};
    std::array<Header, 3> arrays{};
    CameraBinding camera{};
    status = Read(base_, world_, values, manager, owner, arrays, camera);
    if (!status.ok()) return status;
    status=ValidateStage(base_,target?stage_:previous_stage_,true,nullptr,stage_scheduler_,stage_scheduler_restore_);if(!status.ok())return status;
    const auto& expected_arrays = target ? arrays_ : previous_arrays_;
    if (manager != manager_ || owner != owner_ || camera != camera_binding_ || values != (target ? values_ : previous_values_)
        || std::memcmp(arrays.data(), expected_arrays.data(), sizeof(Header) * 3))
        return Status::failure(FailureCode::RestoreVerificationFailed);
    __try
    {
        const auto expected_epoch = target ? (values_.timers_admitted ? epoch_ : epoch_ - 1) : previous_timer_epoch_;
        if (ReadAt<std::uint64_t>(manager, 0x110) != expected_epoch)
            return Status::failure(FailureCode::RestoreVerificationFailed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return Status::failure(FailureCode::ContextUnavailable); }
    // Validate installed headers before dereferencing retained native backing.
    std::uint64_t fingerprint{};
    status = Fingerprint(base_, arrays, fingerprint);
    if (status.ok() && fingerprint != (target ? target_fingerprint_ : previous_fingerprint_))
        status = Status::failure(FailureCode::RestoreVerificationFailed);
    return status;
}

Status Sc6ReplayWorldState::PreparedRestore::Publish() noexcept
{
    if (published_) return Status::failure(FailureCode::IllegalTransition);
    auto status = ValidateImage(false);
    if (!status.ok()) return status;
    std::uint64_t fingerprint{};
    status = Fingerprint(base_, arrays_, fingerprint);
    if (!status.ok()) return status;
    if (fingerprint != target_fingerprint_) return Status::failure(FailureCode::RestorePreflightFailed);
    published_ = true; // Partial header writes retain both graphs too.
    write_complete_ = Write(base_, world_, manager_, values_, arrays_, values_.timers_admitted ? epoch_ : epoch_ - 1, camera_binding_)
        && WriteStage(base_,stage_);
    status = write_complete_ ? ValidatePublished() : Status::failure(FailureCode::RestoreWriteFailed);
    if (!status.ok() && !Undo().ok()) return Status::failure(FailureCode::UndoFailed);
    return status;
}

Status Sc6ReplayWorldState::PreparedRestore::ValidatePublished() const noexcept
{
    return published_ && !undo_started_ && (!executing_ || execution_settled_)
        ? ValidateImage(true) : Status::failure(FailureCode::IllegalTransition);
}

Status Sc6ReplayWorldState::PreparedRestore::BeginExecution(std::size_t retirement_budget,const Sc6ReplaySchedulerState* scheduler,
    const Sc6ReplaySchedulerState::PreparedRestore* scheduler_restore) noexcept
{
    if(executing_ || bool(scheduler)!=bool(scheduler_restore)
        || retirement_budget>std::numeric_limits<std::size_t>::max()-owned_bytes_)
        return Status::failure(FailureCode::IllegalTransition);
    auto status=ValidatePublished();if(!status.ok())return status;
    std::uint64_t previous{};
    status=Fingerprint(base_,previous_arrays_,previous);
    if(!status.ok() || previous!=previous_fingerprint_)return Status::failure(FailureCode::RestoreVerificationFailed);
    status=ValidateExecutionStorage(base_,arrays_,previous_arrays_,retirement_budget);
    if(!status.ok())return status;
    // Native timer execution moves records, destroys delegates, and reallocates
    // arrays. Old A headers cease to be retirement addresses at this point.
    stage_scheduler_=scheduler;stage_scheduler_restore_=scheduler_restore;
    arrays_={};executing_=true;execution_retirement_budget_=retirement_budget;
    owned_bytes_+=retirement_budget;
    return Status::success();
}

Status Sc6ReplayWorldState::PreparedRestore::ValidateExecutionStorage(std::uintptr_t base,
    const std::array<Header,3>& current,const std::array<Header,3>& previous,std::size_t budget) noexcept
{
    __try {
        constexpr std::array<std::uint8_t,3> states{1,2,0};
        std::size_t callbacks{},arrays{};
        for(std::size_t i=0;i<current.size();++i) {
            const auto& h=current[i];
            if(h.count<0 || h.capacity<h.count || bool(h.capacity)!=bool(h.data))
                return Status::failure(FailureCode::RestorePreflightFailed);
            const auto requested=std::size_t(h.capacity)*sizeof(Record);
            if(requested>budget-arrays)return Status::failure(FailureCode::CapacityExceeded);
            if(requested) {
                const auto bytes=reinterpret_cast<std::size_t(*)(std::size_t,unsigned)>(base+0xd50dc0)(requested,0);
                if(bytes<requested || bytes>budget-arrays)return Status::failure(FailureCode::CapacityExceeded);
                arrays+=bytes;
            }
            if(callbacks>budget-arrays)return Status::failure(FailureCode::CapacityExceeded);
            for(int j=0;j<h.count;++j) {
                const auto s=ValidateRecord(base,h.data[j],states[i],callbacks);if(!s.ok())return s;
                if(callbacks>budget-arrays)return Status::failure(FailureCode::CapacityExceeded);
            }
        }
        // Both supported callback types own their distinct 48-byte allocation.
        // Check every allocation, including cross-kind overlap, before native
        // destruction can be scheduled. Do not compare changed timer values to A.
        const auto visit=[](const auto& graph,auto visitor) {
            for(const auto& h:graph) {
                if(h.capacity && !visitor(h.data,std::size_t(h.capacity)*sizeof(Record)))return false;
                for(int j=0;j<h.count;++j)if(ReadAt<int>(&h.data[j],0x40))
                    if(!visitor(ReadAt<void*>(&h.data[j],0x30),48))return false;
            }
            return true;
        };
        std::size_t ordinal{};
        const auto disjoint=visit(current,[&](const void* p,std::size_t size) {
            const auto a=reinterpret_cast<std::uintptr_t>(p);const auto own=ordinal++;
            if(!a || size>UINTPTR_MAX-a)return false;
            const auto no_overlap=[&](const void* q,std::size_t length) {
                const auto b=reinterpret_cast<std::uintptr_t>(q);
                return b && length<=UINTPTR_MAX-b && !(a<b+length && b<a+size);
            };
            if(!visit(previous,no_overlap))return false;
            std::size_t other{};
            return visit(current,[&](const void* q,std::size_t length) {
                const auto index=other++;return index==own || no_overlap(q,length);
            });
        });
        return disjoint?Status::success():Status::failure(FailureCode::RestorePreflightFailed);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayWorldState::PreparedRestore::SettleExecution() noexcept
{
    if(!executing_ || execution_settled_ || !published_ || undo_started_)
        return Status::failure(FailureCode::IllegalTransition);
    __try {
        const auto epoch=ReadAt<std::uint64_t>(reinterpret_cast<void*>(base_),0x4197170);
        auto status=ValidateBinding(epoch);if(!status.ok())return status;
        std::uint64_t before{};
        status=Fingerprint(base_,previous_arrays_,before);
        if(!status.ok() || before!=previous_fingerprint_)return Status::failure(FailureCode::RestoreVerificationFailed);
        Values values{};void* manager{};void* owner{};std::array<Header,3> current{};CameraBinding camera{};
        status=Read(base_,world_,values,manager,owner,current,camera);if(!status.ok())return status;
        if(manager!=manager_ || owner!=owner_ || camera!=camera_binding_) {
            binding_diagnostic_={"settlement_camera_manager"};
            return Status::failure(FailureCode::GenerationMismatch);
        }
        status=ValidateExecutionStorage(base_,current,previous_arrays_,execution_retirement_budget_);
        if(!status.ok())return status;
        std::uint64_t fingerprint{};
        status=Fingerprint(base_,current,fingerprint);if(!status.ok())return status;
        status=ValidateBinding(epoch);if(!status.ok())return status;
        // 142187B20 and 14217E7C0 consume equality with the physical epoch.
        // Preserve B's admission relation at the NEW epoch; never rewind the
        // global engine counter or reinstall an obsolete physical stamp.
        epoch_=epoch;previous_timer_epoch_=previous_values_.timers_admitted?epoch:epoch-1;
        for(auto& row:stage_) {
            ReplayStageVisibility observed{};status=ReadStage(base_,row.actor,observed);
            unsigned owned_registration{};
            if(stage_scheduler_)for(unsigned i=0;i<row.component_count;++i)
                if(stage_scheduler_->OwnsStageComponent(row.components[i]))owned_registration|=1u<<i;
            if(!status.ok() || !row.SameBinding(observed,owned_registration)) {
                binding_diagnostic_={"settlement_stage",row.actor,0,0,row.values.break_state,observed.values.break_state};
                return Status::failure(FailureCode::GenerationMismatch);
            }
            row=observed;
        }
        arrays_=current;values_=values;target_fingerprint_=fingerprint;
        execution_settled_=true;return Status::success();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return Status::failure(FailureCode::ContextUnavailable);}
}

Status Sc6ReplayWorldState::PreparedRestore::ReopenExecutionForUndo() noexcept
{
    if(!executing_ || !published_ || undo_started_) return Status::failure(FailureCode::IllegalTransition);
    if(!execution_settled_) return Status::success();
    auto status=ValidatePublished();if(!status.ok()) return status;
    std::uint64_t fingerprint{};
    status=Fingerprint(base_,previous_arrays_,fingerprint);
    if(!status.ok() || fingerprint!=previous_fingerprint_) return Status::failure(FailureCode::RestoreVerificationFailed);
    // Relinquish only C retirement metadata. Native teardown may mutate these
    // arrays before recovery captures them again; no B pointer or value moves.
    arrays_={};execution_settled_=false;
    return Status::success();
}

Status Sc6ReplayWorldState::PreparedRestore::Undo() noexcept
{
    if (!published_) return Status::success();
    if (!ValidateBinding().ok()) return Status::failure(FailureCode::UndoFailed);
    // A completed installation must still own its original backing. Refuse
    // to free through stale allocation headers after unintended native work.
    if (write_complete_ && !undo_started_ && !ValidatePublished().ok()) return Status::failure(FailureCode::UndoFailed);
    std::uint64_t fingerprint{};
    // B must still be intact before its pointers are made reachable again.
    if (!Fingerprint(base_, previous_arrays_, fingerprint).ok() || fingerprint != previous_fingerprint_)
        return Status::failure(FailureCode::UndoFailed);
    undo_started_ = true; // A failed B write/readback must be retryable as undo.
    if (!Write(base_, world_, manager_, previous_values_, previous_arrays_, previous_timer_epoch_, camera_binding_)
        || !WriteStage(base_,previous_stage_,stage_scheduler_,stage_scheduler_restore_)
        || !ValidateImage(false).ok()) return Status::failure(FailureCode::UndoFailed);
    published_ = false;
    write_complete_ = false;
    undo_started_ = false;
    return Status::success();
}

Status Sc6ReplayWorldState::PreparedRestore::Commit() noexcept
{
    auto status = ValidatePublished();
    if (!status.ok()) return status;
    std::uint64_t fingerprint{};
    status = Fingerprint(base_, previous_arrays_, fingerprint);
    if (!status.ok()) return status;
    if (fingerprint != previous_fingerprint_) return Status::failure(FailureCode::RestoreVerificationFailed);
    arrays_ = previous_arrays_; // Retire B only after the enclosing commit decision.
    published_ = false;
    Clear();
    return Status::success();
}

Status Sc6ReplayWorldState::PreparedRestore::Apply() noexcept
{
    // Compatibility for standalone restoration; the host uses split ownership.
    auto status = Publish();
    if (status.ok()) status = Commit();
    if (!status.ok() && !Undo().ok()) return Status::failure(FailureCode::UndoFailed);
    return status;
}
#include "Sc6ReplayWorldState.Render.inl"
}
