#pragma once
#include <cstdint>
#include <initializer_list>

namespace Horse::Deterministic {
// One shared read-only predicate for application admission and the exact
// PhysX start/substep tasks. Readers return false on inaccessible memory. No callback,
// allocation, weak-reference promotion or native scene publication occurs here.
class ReplayPhysicsStepConsumer final {
public:
    enum class Check : unsigned {None,SceneAbsent,StepCallbacks,SubstepCallbacks,
        StepRecursion,SubstepRecursion,ActiveVehicles,VehiclePair,TickDispatch,WorldRead,SceneRead,
        DelegateStorage,DelegateDispatch,SubstepContext,SubstepCounter};
    struct Diagnostic {Check check{};std::uintptr_t field{},world{},scene{};};
    struct SubstepDiagnostic : Diagnostic {
        std::uintptr_t task_table{},callable{},method{},context{},completion{};
        unsigned count{},limit{};
    };
    static const char* Name(Check check) noexcept {
        switch(check) {
        case Check::None:return "none";
        case Check::SceneAbsent:return "update_physics_scene_absent";
        case Check::StepCallbacks:return "update_physics_step_callbacks";
        case Check::SubstepCallbacks:return "update_physics_substep_callbacks";
        case Check::StepRecursion:return "update_physics_step_recursion";
        case Check::SubstepRecursion:return "update_physics_substep_recursion";
        case Check::ActiveVehicles:return "update_physics_active_vehicles";
        case Check::VehiclePair:return "update_physics_vehicle_pair";
        case Check::TickDispatch:return "physics_start_tick_dispatch";
        case Check::WorldRead:return "physics_start_world";
        case Check::SceneRead:return "physics_start_scene";
        case Check::DelegateStorage:return "physics_substep_delegate_storage";
        case Check::DelegateDispatch:return "physics_substep_delegate_dispatch";
        case Check::SubstepContext:return "physics_substep_context";
        case Check::SubstepCounter:return "physics_substep_counter";
        }
        return "unknown";
    }
    template<class Read> static bool Admit(std::uintptr_t base,std::uintptr_t physics,Read read,Diagnostic& diagnostic) noexcept {
        diagnostic.scene=physics;
        const auto reject=[&](Check check,std::uintptr_t field){diagnostic.check=check;diagnostic.field=field;return false;};
        if(!physics)return reject(Check::SceneAbsent,0);
        std::uintptr_t vehicle_owner{};unsigned vehicle_pair{};
        for(unsigned collection:{0x10u,0x80u}) {
            const auto callbacks=physics+collection;
            const auto callback_failure=[&]{return reject(collection==0x10?Check::StepCallbacks:Check::SubstepCallbacks,callbacks+0x50);};
            int count{},recursion{};
            if(!read(callbacks+0x50,count) || count<0 || count>1)return callback_failure();
            if(!read(callbacks+0x64,recursion) || recursion)
                return reject(collection==0x10?Check::StepRecursion:Check::SubstepRecursion,callbacks+0x64);
            if(!count)continue;
            // Exact raw vehicle delegates are required native calls. Their
            // verified methods return before effects only with zero vehicles.
            std::uintptr_t entry{},callable{},type{},invoke{},owner{},method{};
            std::uint64_t handle{},registered{};int units{},vehicles{};
            if(!read(callbacks+0x40,entry))return callback_failure();
            if(!entry)entry=callbacks;
            if(!read(entry+0x30,units) || units!=3 || !read(entry+0x20,callable) || !callable
                || !read(callable,type) || type!=base+0x3510a68
                || !read(type+0x68,invoke) || invoke!=base+0x2017dc0
                || !read(callable+8,owner) || !owner || !read(callable+0x10,method)
                || method!=base+(collection==0x10?0x30fc2b0:0x30fe7a0)
                || !read(callable+0x20,handle) || !handle
                || !read(owner+(collection==0x10?0x60:0x68),registered) || registered!=handle
                || (vehicle_owner && vehicle_owner!=owner) || !VehicleOwner(base,physics,owner,read))return callback_failure();
            if(!read(owner+0x10,vehicles) || vehicles)return reject(Check::ActiveVehicles,callbacks+0x50);
            vehicle_owner=owner;vehicle_pair|=collection==0x10?1:2;
        }
        if(vehicle_pair && vehicle_pair!=3)return reject(Check::VehiclePair,physics+(vehicle_pair==1?0xd0:0x60));
        diagnostic.check=Check::None;diagnostic.field=0;return true;
    }
    template<class Read> static bool StartTask(std::uintptr_t base,std::uintptr_t tick,Read read,Diagnostic& diagnostic) noexcept {
        diagnostic={};std::uintptr_t table{},dispatch{};
        if(!read(tick,table) || table!=base+0x39efa78 || !read(table+8,dispatch) || dispatch!=base+0x2018570) {
            diagnostic.check=Check::TickDispatch;diagnostic.field=tick;return false;
        }
        if(!read(tick+0x50,diagnostic.world) || !diagnostic.world) {
            diagnostic.check=Check::WorldRead;diagnostic.field=tick+0x50;return false;
        }
        if(!read(diagnostic.world+0x1c8,diagnostic.scene)) {
            diagnostic.check=Check::SceneRead;diagnostic.field=diagnostic.world+0x1c8;return false;
        }
        // Shipped142018570 returns immediately for a null scene. Preserve it.
        return !diagnostic.scene || Admit(base,diagnostic.scene,read,diagnostic);
    }
    template<class Read> static bool SubstepTask(std::uintptr_t base,std::uintptr_t task,Read read,SubstepDiagnostic& diagnostic) noexcept {
        diagnostic={};
        const auto reject=[&](Check check,std::uintptr_t field){diagnostic.check=check;diagnostic.field=field;return false;};
        if(!read(task,diagnostic.task_table))return reject(Check::DelegateStorage,task);
        const bool initial=diagnostic.task_table==base+0x3657428;
        const bool repeat=diagnostic.task_table==base+0x36bc160;
        if(!initial && !repeat)return true;
        int units{};
        if(!read(task+0x40,units))return reject(Check::DelegateStorage,task+0x40);
        // Native dispatch does nothing when empty. Smaller delegate storage
        // cannot contain either known raw substep callable; leave it native.
        if(units>=0 && units<2)return true;
        if(!read(task+0x30,diagnostic.callable))return reject(Check::DelegateStorage,task+0x30);
        if(!diagnostic.callable)diagnostic.callable=task+0x10;
        if(!read(diagnostic.callable+0x10,diagnostic.method))return reject(Check::DelegateStorage,diagnostic.callable+0x10);
        if(diagnostic.method!=base+0x20555e0 && diagnostic.method!=base+0x2055bc0)return true;
        std::uintptr_t dispatch{},table{},valid{},invoke{};
        if(units!=3 || diagnostic.method!=base+(initial?0x20555e0:0x2055bc0)
            || !read(diagnostic.task_table+8,dispatch) || dispatch!=base+(initial?0x14b0ff0:0x15ecb80)
            || !read(diagnostic.callable,table) || table!=base+(initial?0x39767c0:0x3510a68)
            || !read(table+0x38,valid) || valid!=base+0x2d72f0
            || !read(table+0x68,invoke) || invoke!=base+(initial?0x2017de0:0x2017dc0))
            return reject(Check::DelegateDispatch,diagnostic.callable);
        // Both verified bound delegates have unconditional IsValid=true.
        // No sequence handle or unconsumed member-pointer bytes are policy.
        read(task+0x68,diagnostic.completion); // Diagnostic only; never release.
        if(!read(diagnostic.callable+8,diagnostic.context) || !diagnostic.context)
            return reject(Check::SubstepContext,diagnostic.callable+8);
        if(repeat) {
            if(!read(diagnostic.context+0xa0,diagnostic.limit) || !read(diagnostic.context+0xc4,diagnostic.count))
                return reject(Check::SubstepCounter,diagnostic.context+0xc4);
            // 142055BC0 uses unsigned JNC. Its final branch releases C8/B0
            // without calling C40 or consuming callbacks; preserve retirement.
            if(diagnostic.count>=diagnostic.limit)return true;
        }
        if(!read(diagnostic.context+0xd0,diagnostic.scene))return reject(Check::SceneRead,diagnostic.context+0xd0);
        // Null D0 skips only callbacks, not simulation or event obligations.
        // Forward the entire original wrapper unchanged in that native case.
        return !diagnostic.scene || Admit(base,diagnostic.scene,read,diagnostic);
    }
private:
    template<class Read> static bool VehicleOwner(std::uintptr_t base,std::uintptr_t physics,std::uintptr_t owner,Read read) noexcept {
        const auto map=base+0x40e90a0;
        struct Array {std::uintptr_t data{};int count{},capacity{};} rows;
        int free{},bits{},capacity{};std::uintptr_t flags{};
        if(!owner || !read(map,rows) || rows.count<=0 || rows.count>64 || rows.capacity<rows.count
            || rows.capacity>4096 || !rows.data || !read(map+0x34,free) || free<0 || free>rows.count
            || !read(map+0x28,bits) || bits!=rows.count || !read(map+0x2c,capacity) || capacity<bits
            || !read(map+0x20,flags))return false;
        if(!flags)flags=map+0x10;
        unsigned occupied{},matches{};
        for(int i=0;i<rows.count;++i) {
            unsigned word{};
            if(!read(flags+unsigned(i/32)*4,word))return false;
            if(!((word>>(i%32))&1))continue;
            ++occupied;std::uintptr_t key{},value{};
            if(!read(rows.data+std::uintptr_t(i)*0x18,key) || !read(rows.data+std::uintptr_t(i)*0x18+8,value))return false;
            if(key==physics){if(value!=owner)return false;++matches;}
        }
        return occupied==unsigned(rows.count-free) && matches==1;
    }
};
}
