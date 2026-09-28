// Executes production handoff methods with deterministic validation backends.
// It does not replace native resource/lifetime validation or claim GPU proof.
namespace RenderReopenTest {
struct Sc6ReplayParticleCopy {
    enum class RestoreSettlement {CommitCurrent,RecoverOriginal};
    bool execution_started_=true,execution_settled_=true;
    RestoreSettlement execution_settlement_=RestoreSettlement::CommitCurrent;
    struct {bool retired()const{return true;}} completion_;
    struct {bool native_write_uncommitted=true,execution_work_complete=true;} witness_;
    std::uintptr_t world_=1,view_state_=1;int execution_selection_=0;
    bool lighting_executing_=true,lighting_execution_settled_=true,lighting_dirty_=true,lighting_transferred_=false;
    bool visibility_executing_=true,visibility_execution_settled_=true,visibility_dirty_=true;
    bool visibility_storage_published_=true,material_dirty_=true;
    std::size_t visibility_native_a_refs_=1;
    std::array<void*,3> visibility_installed_{{reinterpret_cast<void*>(2),nullptr,nullptr}};
    struct Lighting {
        struct Allocation {void* installed=reinterpret_cast<void*>(1);};
        struct Map {std::array<void*,3> installed{{reinterpret_cast<void*>(3),nullptr,nullptr}};};
        std::array<Allocation,1> allocations;std::array<Map,1> maps;
    } lighting_a_;
    struct {std::array<int,1> queries;} visibility_a_;
    bool light_valid=true,visibility_valid=true,b_valid=true;
    struct Registry {
        bool valid=true,settled=true;unsigned transfers=0;
        bool ContinueExecution(){if(!valid || !settled)return false;settled=false;++transfers;return true;}
    } birth_registry_storage_;
    bool Bindings(void*,bool)const{return true;}
    bool SelectionMatches(int)const{return true;}
    bool BirthOwnersBinding()const{return true;}
    bool LightingImageMatches(bool)const{return light_valid;}
    bool LightingPrivateUndoMatches()const{return b_valid;}
    template<class T> bool VisibilityMatches(const T&)const{return visibility_valid;}
    bool CanBeginLightingExecution()const noexcept;void TransferLightingExecution()noexcept;
    bool CanBeginVisibilityExecution()const noexcept;void TransferVisibilityExecution()noexcept;
    bool BeginLightingExecution()noexcept;bool BeginVisibilityExecution()noexcept;
    bool ContinueExecutionForUndo()noexcept;
};
#include "../HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.Reopen.inl"
static void Run() {
    for(unsigned failure=0;failure<4;++failure) {
        Sc6ReplayParticleCopy p;
        if(failure==0)p.light_valid=false;
        if(failure==1)p.visibility_valid=false;
        if(failure==2)p.birth_registry_storage_.valid=false;
        if(failure==3)p.b_valid=false;
        for(unsigned retry=0;retry<2;++retry) {
            expect(!p.ContinueExecutionForUndo() && p.execution_settled_
                && p.lighting_execution_settled_ && p.visibility_execution_settled_
                && p.lighting_a_.allocations[0].installed==reinterpret_cast<void*>(1)
                && p.lighting_a_.maps[0].installed[0]==reinterpret_cast<void*>(3)
                && p.visibility_installed_[0]==reinterpret_cast<void*>(2) && p.visibility_native_a_refs_==1
                && p.birth_registry_storage_.settled && p.birth_registry_storage_.transfers==0
                && p.witness_.execution_work_complete,
                "render reopening rejection preserves every settled owner across retry");
        }
        p.light_valid=p.visibility_valid=p.birth_registry_storage_.valid=p.b_valid=true;
        expect(p.ContinueExecutionForUndo() && !p.execution_settled_ && !p.lighting_execution_settled_
            && !p.visibility_execution_settled_ && !p.lighting_a_.allocations[0].installed
            && !p.lighting_a_.maps[0].installed[0] && !p.visibility_installed_[0]
            && p.visibility_native_a_refs_==0 && p.birth_registry_storage_.transfers==1
            && !p.witness_.execution_work_complete,
            "successful render handoff transfers every owner once and requires fresh GPU drain");
        expect(!p.ContinueExecutionForUndo() && p.birth_registry_storage_.transfers==1,
            "completed render handoff cannot repeat registry transfer");
    }
    Sc6ReplayParticleCopy partial;partial.execution_settled_=false;
    expect(!partial.ContinueExecutionForUndo() && partial.birth_registry_storage_.transfers==0,
        "initial partial settlement remains rejected until its capture leases are owned");
}
}
