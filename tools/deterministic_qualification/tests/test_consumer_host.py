import pytest
"""Extracted production control flow; no UObject ownership or live recovery proof."""
import subprocess
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.native_contract
def test_prepared_owner_retirement_after_finish_and_predicate_witness(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    owners=(root/'Sc6ReplayHost.ParticleOwners.inl').read_text()
    start=owners.index('        if(owner.slots_installed)',owners.index('Status Sc6ReplayHost::RetirePreparedFreshParticles('))
    (tmp_path/'prepared_owner_slots.inl').write_text(owners[start:owners.index('        if(owner.slots) {',start)])
    driver=(root/'Sc6ReplayHost.Restore.inl').read_text()
    start=driver.index('        if(transaction.execution &&',driver.index('if (operation.phase == RestoreOperationPhase::Recovered)'))
    (tmp_path/'prepared_owner_gate.inl').write_text(driver[start:driver.index('        if (copy.phase !=',start)])
    copy=(root/'Sc6ReplayParticleCopy.cpp').read_text()
    finish=copy[copy.index('bool Sc6ReplayParticleCopy::Finish()'):]
    (tmp_path/'prepared_owner_finish.inl').write_text('\n'.join(line for line in finish.splitlines() if 'registry_prepared_=registry_executing_=execution_started_=execution_settled_=false;' in line or 'witness_.phase = Phase::Released;' in line))
    fixture=module.ROOT/'tools/replay_prepared_owner_retirement_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:prepared-owner.exe /Fo:prepared-owner.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    for mode in ('gate','slots'):
        child=subprocess.run([str(tmp_path/'prepared-owner.exe'),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert child.returncode==0,(mode,child.returncode,child.stdout,child.stderr)


@pytest.mark.native_contract
def test_second_consumer_failure_preserves_actual_recovery_witness(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('        if(request_.consumer_mutation && state.phase==Phase::Failed) {')
    end=source.index('        if(ParticleLifetimeRecovery() && state.phase==Phase::Failed)',start)
    (tmp_path/'consumer_recovery_observer.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_consumer_recovery_diagnostic_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:consumer-recovery.exe /Fo:consumer-recovery.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    child=subprocess.run([str(tmp_path/'consumer-recovery.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,(child.returncode,child.stdout,child.stderr)


@pytest.mark.native_contract
def test_application_frame_sync_preserves_admitted_waits(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    source=(root/'Sc6ReplayHost.Application.inl').read_text()
    start=source.index('\n',source.index('    const auto sync_start='))+1
    (tmp_path/'frame_sync_call.inl').write_text(source[start:source.index('    if(measure_seek)',start)])
    owned='/DREPLAY_OWNED_FRAME_SYNC' if (root/'Sc6ReplayTaskGroup.FrameSync.inl').is_file() else ''
    fixture=module.ROOT/'tools/replay_frame_sync_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc {owned} /I. /I"{root}" "{fixture}" /Fe:frame-sync.exe /Fo:frame-sync.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    for mode in ('pending','complete','lag','disabled','busy','nested','timeout','x-timeout-disabled',
                 'epoch-cached-timeout','signal-without-completion','mutated-slot','invalid'):
        child=subprocess.run([str(tmp_path/'frame-sync.exe'),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert child.returncode==0,(mode,child.returncode,child.stdout,child.stderr)


@pytest.mark.native_contract
def test_engine_context_worlds_preserve_owned_dispatch(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Engine.inl').read_text()
    start=source.index('bool Sc6ReplayHost::TickEngineWorld()')
    (tmp_path/'engine_world_body.inl').write_text(source[start:source.index('void Sc6ReplayHost::DispatchCauseEvent()',start)])
    fixture=module.ROOT/'tools/replay_engine_world_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:engine-world.exe /Fo:engine-world.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    child=subprocess.run([str(tmp_path/'engine-world.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,(child.returncode,child.stdout,child.stderr)


@pytest.mark.native_contract
def test_world_start_preserves_owned_prior_cleanup_wait(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    world=(root/'Sc6ReplayWorld.cpp').read_text()
    start=world.index('    case Phase::StartTasks:')
    (tmp_path/'prior_cleanup_world.inl').write_text(world[start:world.index('    case Phase::Group0:',start)])
    source=(root/'Sc6ReplayTaskGroup.cpp').read_text()
    start=source.find('std::array<std::uint8_t,3> Sc6ReplayTaskGroup::PriorCleanupFlags()')
    (tmp_path/'prior_cleanup_methods.inl').write_text('' if start<0 else source[start:source.index('void Sc6ReplayTaskGroup::ReleaseGroup()',start)])
    fixture=module.ROOT/'tools/replay_prior_cleanup_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{root}" "{fixture}" /Fe:prior-cleanup.exe /Fo:prior-cleanup.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    for mode in ('','zero','ready','incomplete','manager','config','array','entry','flag'):
        child=subprocess.run([str(tmp_path/'prior-cleanup.exe'),*([mode] if mode else [])],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert child.returncode==0,(mode,child.returncode,child.stdout,child.stderr)


def test_consumer_recovery_requires_native_mutation_and_complete_retirement():
    import pytest
    from replay_test import validate_seek_recovery_ownership, continuation_generation
    from deterministic_qualification.replay_control import expected_historical_rewinds
    from deterministic_qualification.replay_fidelity import bounded_checkpoint_window
    report=dict(consumer_mutation=True,historical_anchor_tick=210,historical_advanced_tick=217,
        host_seek_target=217,host_seek=True,historical_exact_advance=True,historical_cancel='after')
    assert bounded_checkpoint_window(report)
    assert expected_historical_rewinds(report)==[(217,210)]
    assert continuation_generation(report,'after')==1
    assert not bounded_checkpoint_window(dict(report,historical_cancel='before'))
    def state(name,tick,tail=0,executed=0):
        return (f'seek ownership state={name} B_tick=217 target=217 current_tick={tick} completed_tail_tick={tail} '
            f'B_retained={"false" if name=="Recovered" else "true"} commit_decided=false executed_ticks={executed} executed_intervals={executed}')
    raw='\n'.join([
        state('BRetained',217),state('APublished',210),state('ExecutionActive',210),
        'consumer prerequisite mutation injected count=1 native_api=141D58ED0 production_entry_returned=true repair=false',
        'consumer prerequisite changed tick=211 epoch=100 owner_index=5 owner_generation=6 native_completed=true application_complete=true B_retained=true recovery=false',
        state('FailedRecoverable',211),
        'consumer mutation failure observed run_id=c tick=211 undo_tick=217 pending_task=false complete_B=true',
        state('RecoveryQuiescing',211),
        'seek C-only retirement owners=0 completed=0 B_retained=true native_render_drain_required=true',
        'seek render drain completed tick=211 recovery=true gpu_complete=true render_settled=false',
        state('Recovering',211,211,1),state('Recovered',217,211,1)])
    result=validate_seek_recovery_ownership(raw,210,217,211,217,consumer=True)
    assert result['cancellation_boundary']=='ConsumerPrerequisite' and result['executed_ticks']==1
    for broken in (raw.replace('native_completed=true','native_completed=false'),
        raw.replace('gpu_complete=true','gpu_complete=false'),raw.replace('repair=false','repair=true'),
        raw.replace('native_api=141D58ED0','native_api=mock'),raw.replace('count=1','count=2'),
        raw.replace('state=FailedRecoverable','state=Held'),raw.replace('owner_generation=6','owner_generation=0'),
        raw.replace(state('Recovering',211,211,1),state('Recovering',212,212,2)),
        raw.replace('consumer prerequisite changed','missing consumer prerequisite changed',1).replace('missing consumer prerequisite changed','removed'),
        raw+'\n'+raw):
        with pytest.raises(RuntimeError):validate_seek_recovery_ownership(broken,210,217,211,217,consumer=True)


@pytest.mark.workflow
def test_consumer_mutation_cli_stops_before_launch(tmp_path,monkeypatch):
    import replay_test as module
    monkeypatch.setattr(module.sys,'argv',['replay_test.py','combat-restore','--host-seek','--combat-case','after',
        '--combat-exact-advance','--combat-anchor-tick','210','--combat-advanced-tick','217','--host-seek-target','217','--consumer-mutation'])
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module,'ensure_build',lambda *_:{})
    monkeypatch.setattr(module,'live_preflight',lambda *_:{'result':'ready','checks':[]})
    seen=[]
    def capture(args):
        seen.append(args)
        raise RuntimeError('fixture stops before deployment')
    monkeypatch.setattr(module,'capture',capture)
    monkeypatch.setattr(module,'revalidate_advance_failure_capture',capture)
    assert module.main()==1 and len(seen)==1
    assert seen[0].consumer_mutation and seen[0].historical_advanced_tick==217


@pytest.mark.native_contract
def test_prerequisite_guard_reads_and_pre_native_witness(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    source=(include/'NativeReplayPrerequisiteGuard.hpp').read_text()
    (tmp_path/'prerequisite_guard_body.inl').write_text(source[source.index('namespace Horse::Deterministic {'):])
    fixture=module.ROOT/'tools/replay_prerequisite_guard_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{include}" "{fixture}" /Fe:prerequisite.exe /Fo:prerequisite.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'prerequisite.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)
    import json
    for mode,site,reason in [('valid',0,0),('encoded',0,0),('high',3,8),('mismatch',3,8),
        ('absent',3,8),('native',3,8),('bare',3,8),('root',3,3),('stale',3,5),('protocol',3,8),('queue',4,2)]:
        path=tmp_path/'consumer_failure.json';path.unlink(missing_ok=True)
        child=subprocess.run([str(tmp_path/'prerequisite.exe'),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        if mode in ('valid','encoded'):
            assert child.returncode==0 and path.read_bytes()==b''
        else:
            assert child.returncode & 0xffffffff == 0xc0000409,(mode,child.returncode,child.stderr)
            record=json.loads(path.read_text())
            assert (record['site'],record['reason'],record['native_started'])==(site,reason,0),record
            if mode=='protocol':
                assert record['epoch']==7 and record['operands'][7]==6 and record['operands'][15]==0
            if mode=='root':
                assert record['owner_generation']==11 and record['operands'][0]==17 and record['operands'][1]==0 and record['operands'][2]!=0
    for mode in ('linked','zero','fault'):
        path=tmp_path/'consumer_failure.json';path.unlink(missing_ok=True)
        child=subprocess.run([str(tmp_path/'prerequisite.exe'),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert child.returncode & 0xffffffff == 0xc0000409,(mode,child.returncode,child.stderr)
        assert 'all queue/node/event bytes unchanged' in child.stdout
        assert path.read_bytes(), 'RED: owned native queue rejection lacks bounded pre-entry state'
        record=json.loads(path.read_text())
        assert record['site']==9 and record['native_site']==24 and record['native_started']==0
        if mode=='fault':
            assert record['reason']==2
        else:
            assert record['reason']==0 and record['operands'][0]==0x4000001
            assert record['operands'][7]&7==7
            assert record['operands'][10:15]==[2,4,1,3,2]
            assert record['task']==(0 if mode=='zero' else 0xdeadbeef)


@pytest.mark.native_contract
def test_scheduler_lifetime_failure_retains_exact_predicate(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    method=source[source.index('    bool ValidateCapturedSchedulerBindings('):source.index('    bool PrepareCapturedScheduler(')]
    fixture=r'''
#include <Windows.h>
#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#define STR_(x) L##x
#define STR(x) STR_(x)
namespace Horse::Deterministic {
enum class FailureCode {None,GenerationMismatch,UnsupportedContent};
struct Status {FailureCode code;bool ok()const{return code==FailureCode::None;}};
struct Sc6ReplaySchedulerState {
    struct BindingFailure {const char* check{};std::uintptr_t tick{},owner{};std::array<int,2> weak{};unsigned offset{};std::uintptr_t expected{},actual{};};
    int mode{};bool detail_requested{};
    Status ValidateBindings(std::uintptr_t base,void* world,BindingFailure* failure=nullptr) {
        if(!base)return {mode==2?FailureCode::None:FailureCode::GenerationMismatch};
        if(!world)return {mode==3?FailureCode::None:FailureCode::GenerationMismatch};
        if(failure){detail_requested=true;*failure={"tick_prefix",0x1110,0x1000,{1,2},0x48,0x2000,0};}
        return {mode==1?FailureCode::GenerationMismatch:FailureCode::None};
    }
};
}
namespace RC {template<class T>std::wstring to_generic_string(const T&){return L"value";}}
enum class LogLevel {Default,Warning};
struct Output {inline static unsigned details{};
    template<LogLevel L,class... T>static void send(const wchar_t* format,T...) {
        if(std::wstring_view(format).find(L"scheduler lifetime detail")!=std::wstring_view::npos)++details;
    }
};
struct ReplayHost {struct InteriorWitness {void* world;unsigned tick;};};
using Horse::Deterministic::Sc6ReplaySchedulerState;
struct Observer {
    struct Image {Sc6ReplaySchedulerState scheduler;} image;
    Image* application_checkpoint_=&image;
    struct SceneReferenceObservation {std::uintptr_t control{};int strong{},weak{};friend bool operator==(const SceneReferenceObservation&,const SceneReferenceObservation&)=default;};
    SceneReferenceObservation application_scene_reference_{0x2000,1,1};
    struct {std::string run_id="local";} request_;
    unsigned failed{};
    bool ReadPhysicsSceneReference(const void*,SceneReferenceObservation& value) {
        value={0x2000,1,image.scheduler.mode==4?3:2};return image.scheduler.mode!=5;
    }
    void Fail(const char*){++failed;}
''' + method + r'''
};
int main() {
    for(int mode=0;mode<=5;++mode) {
        Observer observer;observer.image.scheduler.mode=mode;Output::details=0;
        const auto result=observer.ValidateCapturedSchedulerBindings({reinterpret_cast<void*>(0x3000),217});
        if(result!=(mode==0)||observer.failed!=(mode!=0))return 80;
        if(mode && (Output::details!=1 || !observer.image.scheduler.detail_requested))return 81;
    }
    return 0;
}
'''
    (tmp_path/'scheduler_lifetime.cpp').write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc scheduler_lifetime.cpp /Fe:lifetime.exe /Fo:lifetime.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'lifetime.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_trace_primary_tick_visitor_validates_in_owning_module(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp').read_text()
    method=source[source.index('    Status VisitPrimaryTicks('):source.index('    // Capture-time GC ownership proof')]
    header=r'''
#include <Windows.h>
#include <vector>
#include <cstdint>
static int module_callback_identity;
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}};
struct Sc6ReplayTraceState {
    const void* callback_table=&module_callback_identity;bool live=true;
    struct State {struct {void* object;}mesh;};
    std::vector<State> states_{{{reinterpret_cast<void*>(0x1000)}},{{reinterpret_cast<void*>(0x2000)}},{{reinterpret_cast<void*>(0x1000)}}};
    Status ValidateBindings() const {return {live&&callback_table==&module_callback_identity};}
    static Status Fail(const char*){return {false};}
''' + method + '\n};\n'
    (tmp_path/'visitor.hpp').write_text(header)
    (tmp_path/'owner.cpp').write_text('#include "visitor.hpp"\nextern "C" __declspec(dllexport) Sc6ReplayTraceState* Create(){return new Sc6ReplayTraceState;}\nextern "C" __declspec(dllexport) void Destroy(Sc6ReplayTraceState* p){delete p;}\n')
    (tmp_path/'observer.cpp').write_text(r'''
#include "visitor.hpp"
int main(){
    auto module=LoadLibraryW(L"owner.dll");if(!module)return 80;
    auto create=reinterpret_cast<Sc6ReplayTraceState*(*)()>(GetProcAddress(module,"Create"));
    auto destroy=reinterpret_cast<void(*)(Sc6ReplayTraceState*)>(GetProcAddress(module,"Destroy"));
    auto* image=create();unsigned calls{};
    const auto visit=+[](void* p,std::uintptr_t mesh){++*static_cast<unsigned*>(p);return mesh==0x1000||mesh==0x2000;};
    if(!image->VisitPrimaryTicks(&calls,visit).ok()||calls!=2)return 81;
    image->live=false;calls=0;
    if(image->VisitPrimaryTicks(&calls,visit).ok()||calls)return 82;
    destroy(image);FreeLibrary(module);return 0;
}
''')
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /LD owner.cpp /Fe:owner.dll\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc observer.cpp /Fe:observer.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'observer.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_surface_routes_completed_game_thread_rendering_without_pumping(tmp_path):
    """Production routing with external native task services; lifecycle tested separately."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Surface.inl').read_text()
    method=source[source.index('bool Sc6ReplayHost::QueueSurface('):source.index('bool Sc6ReplayHost::PollSurface(')]
    fixture=r'''
#include <Windows.h>
#include <intrin.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>
#include <cstdint>
namespace Horse::GameImGui {struct PresentHook {
    static PresentHook& instance(){static PresentHook x;return x;}
    std::size_t replay_surface_bytes(){return 0;}
};}
template<class T>T& EngineField(void* p,std::size_t offset){return *reinterpret_cast<T*>(static_cast<std::byte*>(p)+offset);}
static std::array<std::byte,0x40> task;
static std::array<std::byte,0x58> event;
static std::array<std::byte,0x100> graph;
static std::array<std::byte,0x400> named;
static unsigned queued{},executed{};
template<class R=void,class... A>R EngineNative(std::uintptr_t,std::uintptr_t rva,A...args){
    if constexpr(std::is_same_v<R,std::uintptr_t*>){static std::uintptr_t builder[3];builder[0]=reinterpret_cast<std::uintptr_t>(task.data());return builder;}
    else if constexpr(std::is_same_v<R,void*>)return graph.data();
}
template<class R=void,class... A>R EngineVirtual(void* p,std::size_t slot,A...args){
    if(p==graph.data()){++queued;return;}
    if(p==task.data()&&slot==8){++executed;return;}
    std::abort();
}
struct Sc6ReplayHost {
    enum class SurfaceCommand{Arm,Draw};
    std::uintptr_t image_base_{};void* surface_event_{};std::size_t surface_arm_budget_{};
    DWORD thread_=GetCurrentThreadId();
    SurfaceCommand surface_command_{};std::atomic<bool> surface_result_{};
    std::size_t AdmissionRemaining(){return 1024*1024;}
    static void RunSurfaceTask(void*){}
    bool QueueSurface(SurfaceCommand);
};
''' + method + r'''
int main(){
    std::vector<std::byte> image(0x4400000);auto* b=image.data();Sc6ReplayHost h;h.image_base_=reinterpret_cast<std::uintptr_t>(b);
    EngineField<unsigned>(b,0x406f778)=2;
    EngineField<unsigned char>(b,0x419718c)=1;EngineField<DWORD>(b,0x419716c)=GetCurrentThreadId();
    EngineField<void*>(b,0x4166720)=graph.data();EngineField<void*>(graph.data(),8+2*0x18)=named.data();EngineField<unsigned>(named.data(),0x10)=2;
    EngineField<std::uintptr_t>(task.data(),0)=h.image_base_+0x325ba68;EngineField<int>(task.data(),0xc)=1;EngineField<void*>(task.data(),0x28)=event.data();
    if(!h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Arm))return 61;
    if(executed!=1||queued||h.surface_event_!=event.data())return 62;
    if(h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw)||executed!=1)return 63;
    h.surface_event_=nullptr;EngineField<bool>(b,0x4351840)=true;
    if(h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw))return 64;
    EngineField<bool>(b,0x4351840)=false;EngineField<void*>(b,0x4351848)=reinterpret_cast<void*>(1);
    if(h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw))return 65;
    EngineField<void*>(b,0x4351848)=nullptr;EngineField<int>(named.data(),0x28)=1;
    if(h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw))return 66;
    EngineField<int>(named.data(),0x28)=0;EngineField<DWORD>(b,0x419716c)=GetCurrentThreadId()+1;
    if(h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw))return 67;
    EngineField<DWORD>(b,0x419716c)=GetCurrentThreadId();
    EngineField<unsigned>(b,0x406f778)=3;EngineField<bool>(b,0x4351840)=true;
    EngineField<int>(task.data(),0xc)=1;
    if(!h.QueueSurface(Sc6ReplayHost::SurfaceCommand::Draw)||queued!=1||executed!=1)return 68;
    return 0;
}
'''
    cpp=tmp_path/'surface_dispatch.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:surface.exe /Fo:surface.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'surface.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_consumer_domain_accepts_completed_worker_physics_with_onethread(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/NativeReplayTraceTaskGuard.hpp').read_text()
    methods=source[source.index('    bool ExecutionDomain('):source.index('    bool BindNamedThread(')]
    fixture=r'''
#include <Windows.h>
#include <cstdint>
#include <map>
struct Guard {
    std::uintptr_t base_=0x140000000;
    mutable unsigned domain_failure_{};
    inline static std::map<std::uintptr_t,std::uint64_t> memory;
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){return static_cast<T>(memory[p+offset]);}
''' + methods + r'''
};
int main(){
    Guard guard;auto& m=Guard::memory;const auto base=guard.base_;const std::uintptr_t world=0x10000,physics=0x20000;
    for(auto rva:{0x4166dd8u,0x418acb8u,0x429f400u,0x429f250u,0x43b3020u,0x43b2fc0u})m[base+rva]=1;
    m[base+0x4166dd4]=1;m[base+0x418acb4]=1; // native pool remains enabled
    const auto manager=base+0x43b2fd0,sequencer=base+0x43b2608;
    m[manager]=base+0x39cdb10;m[manager+8]=sequencer;
    m[manager+0x28]=5;m[manager+0x2c]=2;m[manager+0x30]=world;m[manager+0x38]=1;
    m[world+0x1c8]=physics;m[physics+4]=2;
    if(!guard.ExecutionDomain(world))return 61;
    for(auto offset:{0xfeu,0x130u,0x160u,0x198u}){
        m[physics+offset]=1;if(guard.ExecutionDomain(world))return 62;m[physics+offset]=0;
    }
    m[physics]=1;m[physics+4]=3;
    if(!guard.ExecutionDomain(world))return 63;
    for(auto offset:{0x100u,0x140u,0x158u,0x228u}){
        m[physics+offset]=1;if(guard.ExecutionDomain(world))return 64;m[physics+offset]=0;
    }
    if(!guard.ExecutionDomain(world,true))return 66;
    for(auto offset:{0x28u,0x2cu,0x30u,0x38u}){
        const auto old=m[manager+offset];m[manager+offset]=0;
        if(guard.ExecutionDomain(world,true))return 67;m[manager+offset]=old;
    }
    m[sequencer+0x9b4]=1;if(guard.ExecutionDomain(world,true))return 68;
    m[sequencer+0x9b4]=0;
    m[base+0x418acb4]=0;if(guard.ExecutionDomain(world))return 65;
    return 0;
}
'''
    cpp=tmp_path/'consumer_domain.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:domain.exe /Fo:domain.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'domain.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_raw_service_lease_uses_real_nonblocking_lock_and_retirement_tombstone(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/NativeReplayTraceTaskGuard.hpp').read_text()
    methods=source[source.index('    bool AcquireRawService()'):source.index('    struct HookStorage')]
    fixture=r'''
#include <Windows.h>
#include <cstdint>
#include <array>
#include <cstring>
#include <thread>
#include <atomic>
struct Guard {
    std::uintptr_t base_=0x1000,raw_service_{};
    inline static bool raw_service_retired_{};
    inline static unsigned native_calls_{},reads{};
    inline static int initialized=1;
    inline static std::uintptr_t service{};
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){
        ++reads;if(p==0x1000+0x43fd2fc)return static_cast<T>(initialized);
        if(p==0x1000+0x40e3a28)return static_cast<T>(service);
        return *reinterpret_cast<T*>(p+offset);
    }
''' + methods + r'''
};
int main(){
    alignas(16) std::array<unsigned char,0x88> service{};
    auto lock=reinterpret_cast<CRITICAL_SECTION*>(service.data()+0x60);
    InitializeCriticalSection(lock);Guard::service=reinterpret_cast<std::uintptr_t>(service.data());Guard guard;
    if(!guard.AcquireRawService())return 1;
    bool acquired=true;std::thread other([&]{acquired=TryEnterCriticalSection(lock);if(acquired)LeaveCriticalSection(lock);});other.join();
    if(acquired)return 2;guard.ReleaseRawService();
    *reinterpret_cast<int*>(service.data()+0x58)=1;
    if(guard.AcquireRawService()||guard.raw_service_)return 3;
    *reinterpret_cast<int*>(service.data()+0x58)=0;
    std::atomic<bool> entered{},release{};
    std::thread busy([&]{EnterCriticalSection(lock);entered=true;while(!release)SwitchToThread();LeaveCriticalSection(lock);});
    while(!entered)SwitchToThread();
    if(guard.AcquireRawService()||guard.raw_service_)return 4;
    release=true;busy.join();
    if(!guard.AcquireRawService())return 5;guard.ReleaseRawService();DeleteCriticalSection(lock);
    Guard::raw_service_retired_=true;Guard::service=1;const auto reads=Guard::reads;
    if(guard.AcquireRawService()||Guard::reads!=reads)return 6;
    return 0;
}
'''
    cpp=tmp_path/'raw_service_lease.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:lease.exe /Fo:lease.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'lease.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_observer_scheduler_inventory_includes_retained_primary_ticks(tmp_path):
    """Actual observer enumeration; controlled storage is not a lifetime proof."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    method=source[source.index('    bool ObserveSchedulerInventory('):source.index('    bool ObserveApplicationPause(')]
    fixture=r'''
#include <array>
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#define STR(x) x
enum class LogLevel {Default};
struct Output {template<LogLevel L,class... T>static void send(T...) {}};
namespace RC {template<class T>auto to_generic_string(T v){return v;}}
void* GetModuleHandleW(void*){return reinterpret_cast<void*>(0x1000);}
struct Status {bool ok()const{return true;}};
struct Traces {
    std::vector<std::uintptr_t> owners;
    Status VisitPrimaryTicks(void* context,bool(*visit)(void*,std::uintptr_t)) const {
        for(auto owner:owners)if(!visit(context,owner))std::abort();return {};
    }
};
struct Checkpoint {std::shared_ptr<Traces> traces=std::make_shared<Traces>();};
struct Probe {
    struct {int run_id{};}request_;
    struct Tick {std::uintptr_t tick{};}application_scheduler_tick_;
    std::array<std::size_t,3> scheduler_inventory_counts_{};
    std::shared_ptr<Checkpoint> application_checkpoint_=std::make_shared<Checkpoint>();
    template<class T>static bool ReadSchedulerValue(std::uintptr_t p,T& value){std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(T));return true;}
    static bool ReadSchedulerTick(std::uintptr_t,Tick&){return true;}
''' + method + r'''
};
template<class T,class A>void put(A& a,std::size_t offset,T value){std::memcpy(a.data()+offset,&value,sizeof(value));}
template<class A>std::uintptr_t ptr(A& a){return reinterpret_cast<std::uintptr_t>(a.data());}
int main(){
    std::array<unsigned char,0x800> world{};
    std::array<unsigned char,0x140> level{};
    std::array<unsigned char,0x200> registered{},dormant{};
    std::array<unsigned char,16> slot{},prereq1{};
    std::array<unsigned char,32> prereq2{};
    put(world,0x778,ptr(level));put(level,8,ptr(slot));put(level,16,1);put(level,0x30,1);put(level,0x18,1u);
    put(slot,0,ptr(registered)+0x110);
    for(auto* component:{&registered,&dormant}){
        put(*component,0x110,std::uintptr_t{0x2000});put(*component,0x158,ptr(level));put(*component,0x160,ptr(*component));
    }
    put(registered,0x130,ptr(prereq1));put(registered,0x138,1);put(registered,0x13c,1);
    put(dormant,0x130,ptr(prereq2));put(dormant,0x138,2);put(dormant,0x13c,2);
    Probe probe;probe.application_checkpoint_->traces->owners={ptr(registered),ptr(dormant),ptr(dormant)};
    if(!probe.ObserveSchedulerInventory(world.data()))return 80;
    if(probe.scheduler_inventory_counts_!=std::array<std::size_t,3>{1,2,3})return 81;
    probe.application_checkpoint_->traces->owners={ptr(registered)};
    if(!probe.ObserveSchedulerInventory(world.data()) || probe.scheduler_inventory_counts_!=std::array<std::size_t,3>{1,1,1})return 82;
    return 0;
}
'''
    cpp=tmp_path/'scheduler_inventory.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:inventory.exe /Fo:inventory.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'inventory.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_initial_cpp_constructor_witness_is_thread_and_scope_bound(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'RE-UE4SS/UE4SS/src/main_ue4ss_rewritten.cpp').read_text()
    methods=source[source.index('static bool s_wait_for_ue4ss'):source.index('auto get_main_thread_id()')]
    fixture=r'''
#include <Windows.h>
#include <atomic>
#include <thread>
#include <stdexcept>
namespace RC {bool IsInitialCppModStartupThread() noexcept;}
static bool expect_early{},throw_ctor{};static std::atomic<int> failures{},done{};
struct UE4SSProgram {
    inline static std::atomic<bool> cpp_mods_done_loading{true};
    UE4SSProgram(wchar_t*,int) {
        if(RC::IsInitialCppModStartupThread()!=expect_early)++failures;
        std::thread worker([]{if(RC::IsInitialCppModStartupThread())++failures;});worker.join();
        if(throw_ctor)throw std::runtime_error("controlled external constructor failure");
    }
};
unsigned long thread_dll_start(UE4SSProgram* p){if(RC::IsInitialCppModStartupThread())++failures;delete p;++done;return 0;}
''' + methods + r'''
int main(){
    if(RC::IsInitialCppModStartupThread())return 1;
    s_wait_for_ue4ss=true;expect_early=true;process_initialized(nullptr);
    if(RC::IsInitialCppModStartupThread())return 2;
    s_wait_for_ue4ss=false;expect_early=false;process_initialized(nullptr);
    if(RC::IsInitialCppModStartupThread())return 3;
    s_wait_for_ue4ss=true;expect_early=true;throw_ctor=true;
    try{process_initialized(nullptr);return 4;}catch(const std::runtime_error&){}
    if(RC::IsInitialCppModStartupThread())return 5;
    for(unsigned i=0;i<1000&&done.load()!=2;++i)Sleep(1);
    return failures.load()||done.load()!=2?6:0;
}
'''
    cpp=tmp_path/'startup_witness.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:startup-witness.exe /Fo:startup-witness.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'startup-witness.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)


@pytest.mark.native_contract
def test_consumer_retirement_entries_stop_before_native_effects(tmp_path):
    """Actual entry routing/Call predicates; does not certify detour installation or lifetime."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    header=(root/'NativeReplayTraceTaskGuard.hpp').read_text()
    identities=header[header.index('    struct Identity {'):header.index('    NativeReplayTraceTaskGuard()')]
    methods=header[header.index('    bool Matches('):header.index('    inline static std::atomic<NativeReplayTraceTaskGuard*>')]
    entry_start=header.index('        const std::array<std::uint64_t,')
    entries=header[entry_start:header.index('        HMODULE module{};',entry_start)]
    fixture=r'''
#include <Windows.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <tuple>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "ReplayConsumerFailure.hpp"
#include "ReplayPhysicsCallbackObservation.hpp"
using Horse::Deterministic::ReplayConsumerFailure;
using Horse::Deterministic::ReplayPhysicsCallbackObservation;
struct NativeReplayTraceTaskGuard {
''' + identities + methods + r'''
    template<auto F>static std::uint64_t Address(){return reinterpret_cast<std::uint64_t>(F);}
    template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){return *reinterpret_cast<T*>(p+offset);}
    inline static std::atomic<NativeReplayTraceTaskGuard*> active_{};
    inline static SRWLOCK mutex_=SRWLOCK_INIT;unsigned calls_{};bool held_{true},executing_{};
    DWORD thread_=GetCurrentThreadId();std::uintptr_t named_thread_=0xfc8;ExecutionObserver observer_{};
    Binding binding_{};struct {void* function{};} task_;
    inline static std::array<std::uint64_t,64> originals_{};
    inline static unsigned native_calls_{};
    inline static bool raw_service_retired_{};std::uintptr_t raw_service_{};
    bool registered_{true};
    static void Original(void* p){std::printf("native-effect %llu\n",reinterpret_cast<std::uintptr_t>(p));std::fflush(stdout);}
    static void OriginalBool(void* p,bool value){std::printf("native-effect %llu bool=%u\n",reinterpret_cast<std::uintptr_t>(p),unsigned(value));std::fflush(stdout);}
    static void OriginalService(void*,void* p){Original(p);}
    static void* OriginalPop(void* p){Original(p);return p;}
    static void OriginalReason(void* p,unsigned value){std::printf("native-effect %llu reason=%u\n",reinterpret_cast<std::uintptr_t>(p),value);std::fflush(stdout);}
    void Run(std::uintptr_t rva,void* owner,unsigned argument){
        struct Site {std::uintptr_t rva;const unsigned char* bytes;std::size_t count;};
#include "NativeReplayTraceTaskGuard.Signatures.inl"
''' + entries + r'''
        active_.store(registered_?this:nullptr);
        for(std::size_t i=0;i<sites.size();++i)if(sites[i].rva==rva){
            originals_[i]=rva==0xd2b9f0?Address<&OriginalPop>():rva==0x2ddae10?Address<&OriginalService>():rva==0x1d99730?Address<&OriginalBool>():rva==0x3bb550?Address<&OriginalReason>():Address<&Original>();
            if(rva==0xd2b9f0){if(reinterpret_cast<void*(*)(void*)>(entries[i])(owner)!=owner)std::abort();}
            else if(rva==0x2ddae10)reinterpret_cast<void(*)(void*,void*)>(entries[i])(nullptr,owner);
            else if(rva==0x1d99730)reinterpret_cast<void(*)(void*,bool)>(entries[i])(owner,argument!=0);
            else if(rva==0x3bb550)reinterpret_cast<void(*)(void*,unsigned)>(entries[i])(owner,argument);
            else reinterpret_cast<void(*)(void*)>(entries[i])(owner);
            return;
        }
        Original(owner);
    }
};
int main(int argc,char** argv){
    if(argc!=3)return 3;
    if(!ReplayConsumerFailure::Start(L"consumer_failure.json","entry-fixture"))return 6;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    NativeReplayTraceTaskGuard guard;guard.binding_.chara.object=0x1000;guard.binding_.scene.object=0x2000;
    auto rva=std::strtoull(argv[1],nullptr,16);int mode=std::atoi(argv[2]);
    guard.held_=mode!=1&&mode!=6;guard.raw_service_=(mode==0||mode==4||mode==6)?0x1000:0;guard.executing_=mode==2||mode==4;
    guard.registered_=mode!=5;
    if(mode>=7){guard.held_=false;guard.executing_=false;
        guard.observer_={nullptr,
            +[](void*,unsigned,void*,void*,void*)noexcept{return false;},
            +[](void*,unsigned,void*,void*,void*)noexcept{return false;}};
        if(mode==8)guard.observer_.before=+[](void*,unsigned,void*,void*,void*)noexcept{return true;};}
    auto owner=mode==3?0x3000ull:(rva==0x3af4b0||rva==0x1d99730?0x2000ull:0x1000ull);
    if(mode==4){std::thread worker([&]{guard.Run(rva,reinterpret_cast<void*>(owner),7);});worker.join();}
    else guard.Run(rva,reinterpret_cast<void*>(owner),7);
    if(rva==0x2ddae10&&!guard.raw_service_retired_)return 5;
    return guard.calls_||guard.native_calls_?4:0;
}
'''
    cpp=tmp_path/'retirement_entries.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{root}" "{cpp}" /Fe:retirement-entries.exe /Fo:retirement-entries.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    for rva in ('3aeb50','3af4b0','1d99730','3ba850','3bb550','2ddae10','3af580','d2b9f0'):
        for mode in (range(9) if rva=='3aeb50' else range(7 if rva=='2ddae10' else 6)):
            diagnostic=tmp_path/'consumer_failure.json'
            diagnostic.unlink(missing_ok=True)
            checked=subprocess.run([str(tmp_path/'retirement-entries.exe'),rva,str(mode)],cwd=tmp_path,capture_output=True,text=True,timeout=10)
            if mode in (7,8):
                import json
                assert checked.returncode & 0xffffffff == 0xc0000409,(mode,checked.stdout,checked.stderr)
                record=json.loads(diagnostic.read_text())
                assert record['site']==(5 if mode==7 else 6) and record['native_started']==(mode==8)
                assert checked.stdout.count('native-effect')==(mode==8)
            elif mode in (0,4) or (rva=='2ddae10' and mode==6):
                assert checked.returncode & 0xffffffff == 0xc0000409,(rva,checked.returncode,checked.stdout)
                assert 'native-effect' not in checked.stdout
                import json
                assert json.loads(diagnostic.read_text())['site']==7
            else:
                assert checked.returncode==0,(rva,mode,checked.returncode,checked.stdout)
                assert checked.stdout.count('native-effect')==1
                assert diagnostic.read_bytes()==b''
                if rva=='1d99730': assert 'bool=1' in checked.stdout
                if rva=='3bb550': assert 'reason=7' in checked.stdout


@pytest.fixture(scope='module')
def vfx_finish_executable(tmp_path_factory):
    import replay_test as module
    from deterministic_iteration import VCVARS
    tmp_path=tmp_path_factory.mktemp('vfx-finish-native')
    root=module.ROOT/'HorseMod/horselib/deterministic'
    source=(root/'Sc6ReplayVfxState.cpp').read_text()
    start=source.index('Status Sc6ReplayVfxState::ValidateCompletionOwnership(')
    (tmp_path/'vfx_finish_admission.inl').write_text(source[start:source.index('Status Sc6ReplayVfxState::ReadCompletionBindings(',start)])
    start=source.index('Status Sc6ReplayVfxState::PreparedManager::ReadFinishDispatchBinding(')
    (tmp_path/'vfx_finish_binding.inl').write_text(source[start:source.index('Status Sc6ReplayVfxState::PreparedManager::ValidateBinding(',start)])
    host=(root/'Sc6ReplayHost.cpp').read_text()
    start=host.index('Status Sc6ReplayHost::ValidateParticleCompletionOwnership(const Sc6ReplayVfxState&')
    (tmp_path/'vfx_finish_host_ownership.inl').write_text(host[start:host.index('#include "Sc6ReplayHost.CheckpointDisplay.inl"',start)])
    arm=(root/'Sc6ReplayHost.Consumer.inl').read_text()
    start=arm.index('Status Sc6ReplayHost::ArmHistoricalFinishGuard()')
    (tmp_path/'vfx_finish_host_arm.inl').write_text(arm[start:arm.index('bool Sc6ReplayHost::HistoricalConsumerAbortPending()',start)])
    restore=(root/'Sc6ReplayHost.Restore.inl').read_text()
    start=restore.index('bool Sc6ReplayHost::HistoricalExecutionAdmitted(')
    (tmp_path/'vfx_finish_host_execution.inl').write_text(restore[start:restore.index('Status Sc6ReplayHost::StepHistoricalExecution()',start)])
    trace=(root/'Sc6ReplayTraceState.hpp').read_text()
    start=trace.index('    static bool Live(const Id& id)')
    live=trace[start:trace.index('    static bool Equal(',start)]
    start=trace.index('    bool OwnsCompletionReceiver(')
    (tmp_path/'vfx_finish_trace_ownership.inl').write_text(live+trace[start:trace.index('    class Prepared;',start)])
    lease=(root/'Sc6ReplayObjectLease.cpp').read_text()
    start=lease.index('Status Sc6ReplayObjectLease::ValidateObject(')
    (tmp_path/'vfx_finish_object_ownership.inl').write_text(lease[start:lease.index('void Sc6ReplayObjectLease::DiagnoseFailure(',start)])
    # Include the real observer, just as the existing observer fixture does.
    # Only indexed UObject and detour installation services are substituted.
    header=(root/'NativeReplayVfxCompletionObservation.hpp').read_text()
    begin=header.index('    inline static constexpr std::array<std::uintptr_t,22> rvas_')
    (tmp_path/'vfx_finish_observer_sites.inl').write_text(header[begin:header.index('    // Charge simultaneous',begin)])
    (tmp_path/'Unreal').mkdir()
    (tmp_path/'Unreal/UObject.hpp').write_text('''#pragma once
#include <cwchar>
namespace RC::Unreal {
inline unsigned receiver_checks;
struct FName {
    struct Index {unsigned value;unsigned ToUnstableInt() const{return value;}};
    unsigned index{21},number{};
    Index GetComparisonIndex() const{return {index};}unsigned GetNumber() const{return number;}
};
struct UFunction {
    unsigned flags{0x400};struct Script {int count{};int Num() const{return count;}} script;
    FName name;void* pointer{};
    unsigned GetFunctionFlags() const{return flags;}const Script& GetScript() const{return script;}
    FName GetFName() const{return name;}void* GetFuncPtr() const{return pointer;}
};
struct UObject {
    std::uintptr_t table{};unsigned flags{};int index{};UFunction* function{};
    UFunction* GetFunctionByNameInChain(const wchar_t* name) {
        ++receiver_checks;SetLastError(0x998);
        return std::wcscmp(name,L"OnVFxFinished")==0?function:nullptr;
    }
};
}
''')
    (tmp_path/'Unreal/UObjectArray.hpp').write_text('''#pragma once
#include "UObject.hpp"
namespace RC::Unreal {
struct Item {UObject* object{};int serial{};UObject* GetUObject(){return object;}
bool IsValid(bool){return object!=nullptr;}int GetSerialNumber(){return serial;}};
struct FUObjectArray {inline static Item items[16];
static Item* IndexToObject(int i){return i>=0&&i<16?&items[i]:nullptr;}};
}
''')
    (tmp_path/'polyhook2/Detour').mkdir(parents=True)
    (tmp_path/'polyhook2/Detour/x64Detour.hpp').write_text('''#pragma once
#include <optional>
namespace PLH {class x64Detour {char retained_size_reserve[1024];
protected: std::uint64_t m_fnAddress{};unsigned m_hookSize{},m_chosen_scheme{1};
struct Disassembler {template<class T>std::uint64_t disassemble(std::uint64_t p,std::uint64_t,std::uint64_t,T&){return p;}} m_disasm;
static std::optional<std::uint64_t> calcNearestSz(std::uint64_t p,std::uint64_t,std::uint64_t& n){return p;}
static bool expandProlSelfJmps(std::uint64_t&,std::uint64_t,std::uint64_t&,std::uint64_t&){return true;}
public: enum {VALLOC2=1};
x64Detour(std::uint64_t target,std::uint64_t entry,std::uint64_t* original)
:m_fnAddress(target){*original=ProbeInstall(target,entry);}
virtual ~x64Detour()=default;
void setDetourScheme(int){}virtual bool hook(){return true;}};}
''')
    fixture=module.ROOT/'tools/replay_vfx_finish_dispatch_selftest.cpp'
    generated=module.ROOT/'build_cmake_LessEqual421__Shipping__Win64/HorseMod/generated'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{root}" /I"{generated}" "{fixture}" /Fe:vfx-finish.exe /Fo:vfx-finish.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    return tmp_path/'vfx-finish.exe'


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('unchanged','empty','unrelated'))
def test_vfx_finish_dispatch_uses_production_observer(tmp_path,vfx_finish_executable,mode):
    """Forwarding alone cannot prove the production Dispatch owner was exercised."""
    import json
    child=subprocess.run([str(vfx_finish_executable),mode],cwd=tmp_path,
                         capture_output=True,text=True,timeout=10)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert child.stdout.count('native-snapshot')==1,detail
    _check_vfx_finish_observer(tmp_path,child,mode)


def _check_vfx_finish_observer(tmp_path,child,mode):
    import json,re
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    detail=child.stdout+child.stderr
    path=tmp_path/'vfx_completion_observation.json'
    assert path.is_file(),'production Dispatch owner did not publish entry/return metadata\n'+detail
    value=json.loads(path.read_bytes())
    expected=[] if mode=='unrelated' else [(0x3d74d0,'entry'),(0x3d74d0,'return')]
    assert [(row['entry_rva'],row['boundary']) for row in value['events']]==expected,value
    assert value['observation_only'] and not value['writer_coverage_proven']
    assert not value['synchronized_snapshot'] and value['pending_pairs']==0
    assert value['phase']['state']=='open' and not value['phase']['complete']
    identity=re.search(r'forwarding-identity collection=(\d+) storage=(\d+) payload=(\d+) thread=(\d+)',child.stdout)
    assert identity,detail
    collection,storage,payload,thread=map(int,identity.groups())
    rows=[] if mode=='empty' else [[7,11,21],[8,12+(mode=='receiver'),21+((mode=='fname-number')<<32)]]
    for event in value['events']:
        assert event['route']=='dispatch' and event['pair']==1 and event['parent_pair']==0
        assert event['caller'] and event['thread']==thread and event['collection']==collection
        assert event['storage']==storage and event['payload']==payload
        assert event['header_after']==[storage,len(rows),len(rows)]
        assert event['count']==event['capacity']==len(rows) and event['rows']==rows
        assert event['rows_complete'] and not event['unstable'] and not event['truncated']
        assert event['manager']['valid'] and event['manager']['index']==1 and event['manager']['serial']==31
        assert [r['valid'] for r in event['row_identities']]==([] if mode=='empty' else [True,mode!='receiver'])
    # Capture admission, arming and dispatch use the same production predicate.
    checks=len(rows)*(2 if mode=='unrelated' else 3)
    assert f'forwarding-values entries=1 rows={len(rows)} last_error=1905 receiver_checks={checks}' in child.stdout,detail
    assert (tmp_path/'consumer_failure.json').read_bytes()==b''
    retained={};retain_vfx_completion_observation(retained,path,'vfx-finish-dispatch-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_completion_observation_prefix_valid'] and not retained['vfx_completion_observation_phase_complete']


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('receiver','fname-number'))
def test_vfx_finish_dispatch_revalidates_live_completion_rows(tmp_path,vfx_finish_executable,mode):
    """Retained RED boundary: terminal containment is still not B recovery."""
    import json
    # Each distinct mutation is individually selectable. No xfail or fixture
    # guard turns the retained missing pre-effect enforcement into a pass.
    child=subprocess.run([str(vfx_finish_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert 'production host finish arming and captured admission passed; indexed receiver/function predicate exercised' in child.stdout,detail
    if child.returncode==125:
        assert child.stdout.count('native-snapshot')==1,detail
    assert child.returncode & 0xffffffff == 0xc0000409,detail
    assert 'native-snapshot' not in child.stdout,detail
    failure=json.loads((tmp_path/'consumer_failure.json').read_text())
    assert failure['site']==12 and failure['native_site']==0x3d74d0 and failure['native_started']==0
    assert failure['reason']==6 and failure['owner_index']==1 and failure['owner_generation']==31
    assert failure['operands'][1:3]==[2,2]
    assert failure['operands'][3:9]==[7,11,21,8,12,21]


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('receiver','fname-number'))
@pytest.mark.parametrize('diagnostic',('off','closed','exhausted'))
def test_vfx_finish_containment_is_independent_of_diagnostics(tmp_path,vfx_finish_executable,mode,diagnostic):
    import json
    child=subprocess.run([str(vfx_finish_executable),mode,diagnostic],cwd=tmp_path,capture_output=True,text=True,timeout=20)
    detail=child.stdout+child.stderr
    assert child.returncode & 0xffffffff == 0xc0000409,detail
    # Exhaustion controls forward exactly once; the final mutated dispatch never does.
    assert child.stdout.count('native-snapshot')==(257 if diagnostic=='exhausted' else 0),detail
    failure=json.loads((tmp_path/'consumer_failure.json').read_text())
    assert failure['site']==12 and failure['reason']==6 and failure['native_started']==0
    path=tmp_path/'vfx_completion_observation.json'
    if diagnostic=='off':
        assert not path.exists()
    else:
        observation=json.loads(path.read_text())
        if diagnostic=='closed':
            assert observation['phase']['state']=='closed' and not observation['events']
        else:
            assert len(observation['events'])==512 and observation['dropped']>0


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('missing-hook','membership','receiver','fname-number','function','duplicate'))
def test_vfx_finish_host_admission_rejects_unsupported_binding(tmp_path,vfx_finish_executable,mode):
    child=subprocess.run([str(vfx_finish_executable),'admit-'+mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'production host finish admission rejected; B comparison unchanged; native_entries=0' in child.stdout
    assert 'native-snapshot' not in child.stdout
    assert (tmp_path/'consumer_failure.json').read_bytes()==b''


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('unregistered','registry','missing','replaced','invalidated','gc-busy'))
def test_vfx_finish_host_admission_requires_retained_receiver(tmp_path,vfx_finish_executable,mode):
    """Live identity/native function cannot substitute for registered owner membership.

    Runs actual host arming, trace receiver admission and ValidateObject. Only
    native GC admission/registry/liveness services are controlled. The receiver
    remains live, captured and correctly named in every negative case. This
    proves a necessary admission check, not exclusion across native effects.
    """
    child=subprocess.run([str(vfx_finish_executable),'admit-owner-'+mode],cwd=tmp_path,
                         capture_output=True,text=True,timeout=10)
    detail=child.stdout+child.stderr
    assert child.returncode==0,detail
    assert 'production host finish admission rejected; B comparison unchanged; native_entries=0' in child.stdout
    assert 'native-snapshot' not in child.stdout
    assert (tmp_path/'consumer_failure.json').read_bytes()==b''


@pytest.mark.native_contract
def test_vfx_finish_execution_requires_armed_guard(tmp_path,vfx_finish_executable):
    child=subprocess.run([str(vfx_finish_executable),'execution-admission'],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'production execution admission requires armed finish guard; B still retained' in child.stdout
    assert 'native-snapshot' not in child.stdout


@pytest.mark.native_contract
@pytest.mark.parametrize(('mode','reason'),(
    ('epoch',2),('manager',3),('receiver-lifetime',7),('function',7),('membership',7),
    ('count',4),('capacity',4),('backing',4),('unreadable',5),('order',6),('duplicate',6),('thread',1),('reentry',8)))
def test_vfx_finish_dispatch_contains_changed_contract(tmp_path,vfx_finish_executable,mode,reason):
    import json
    child=subprocess.run([str(vfx_finish_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=child.stdout+child.stderr
    assert 'production host finish arming and captured admission passed' in child.stdout,detail
    assert child.returncode & 0xffffffff == 0xc0000409,detail
    # Reentry begins inside one forwarded outer native call. The nested
    # unsupported call terminates before its own snapshot; no callback is skipped.
    assert child.stdout.count('native-snapshot')==(1 if mode=='reentry' else 0),detail
    failure=json.loads((tmp_path/'consumer_failure.json').read_text())
    assert failure['site']==12 and failure['reason']==reason and failure['native_started']==0


@pytest.mark.native_contract
def test_consumer_guard_checks_real_indexed_contract_before_owner_reads(tmp_path):
    """Production predicates with external object-array service; no lifetime lease claim."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    header=(module.ROOT/'HorseMod/horselib/deterministic/NativeReplayTraceTaskGuard.hpp').read_text()
    identities=header[header.index('    struct Identity {'):header.index('    NativeReplayTraceTaskGuard()')]
    predicates=header[header.index('    template<class T> static T Read('):header.index('    bool InspectTask(')]
    matches=header[header.index('    bool Matches('):header.index('    struct Call {')]
    fixture=r'''
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace RC::Unreal {
struct Item {
    void* object{};int serial{};bool valid{};
    void* GetUObject() const{return object;}
    bool IsValid(bool) const{return valid;}
    int GetSerialNumber() const{return serial;}
};
struct FUObjectArray {
    inline static std::array<Item,5> items{};
    static Item* IndexToObject(int index){return index>=0&&index<5?&items[index]:nullptr;}
};
}
struct GuardPredicates {
''' + identities + predicates + matches + r'''
    std::uintptr_t base_=0x140000000ull;
    bool held_{true},executing_{};std::uintptr_t raw_service_{};
    DWORD thread_=GetCurrentThreadId();std::uintptr_t named_thread_=0xfc8;ExecutionObserver observer_{};
    Binding binding_{};
    struct {void* function{};} task_;
};
template<class T>void Put(std::uintptr_t p,std::size_t offset,T value){std::memcpy(reinterpret_cast<void*>(p+offset),&value,sizeof(value));}
static unsigned faults;
static LONG WINAPI CountFault(EXCEPTION_POINTERS* p){if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)++faults;return EXCEPTION_CONTINUE_SEARCH;}
int main() {
    GuardPredicates guard;
    std::array<std::array<std::byte,0x1000>,5> storage{};
    std::array<GuardPredicates::Identity,5> ids{};
    for(int i=0;i<5;++i){ids[i]={reinterpret_cast<std::uintptr_t>(storage[i].data()),i,100+i};RC::Unreal::FUObjectArray::items[i]={storage[i].data(),100+i,true};}
    GuardPredicates::Binding binding{ids[0],ids[1],ids[2],ids[3],ids[4]};
    auto c=ids[0].object,m=ids[1].object,a=ids[2].object,s=ids[3].object,w=ids[4].object;
    Put(c,0,guard.base_+0x3360ca8);Put(m,0,guard.base_+0x3361f98);
    Put(a,0,guard.base_+0x3268078);Put(s,0,guard.base_+0x3360370);
    Put(c,0x190,m);Put(c,0x1c8,w);Put(c,0x490,a);Put(m,0x3a8,c);
    Put(a,0x458,m);Put(a,0x168,s);Put(s,0x190,a);
    Put(c,0x110,guard.base_+0x3865f98);Put(c,0x160,c);
    Put(w,0x1c8,std::uintptr_t{0x100});Put(w,0x778,std::uintptr_t{0x200});
    std::uint64_t original{},after{};
    if(!guard.InspectContract(binding,original))return 1;
    for(auto root:{a,s}){auto table=GuardPredicates::Read<std::uintptr_t>(root);Put(root,0,std::uintptr_t{0x1234});if(guard.InspectContract(binding,after))return 11;Put(root,0,table);}
    guard.binding_=binding;
    for(auto root:{a,s})if(!guard.Matches(8,reinterpret_cast<void*>(root),nullptr))return 12;
    if(guard.Matches(8,reinterpret_cast<void*>(0x1234),nullptr))return 13;
    guard.executing_=true;if(guard.Matches(8,reinterpret_cast<void*>(s),nullptr))return 14;guard.executing_=false;
    for(int i=0;i<5;++i){auto& item=RC::Unreal::FUObjectArray::items[i];++item.serial;if(guard.InspectContract(binding,after))return 2;--item.serial;item.valid=false;if(guard.InspectContract(binding,after))return 3;item.valid=true;}
    const std::array<std::pair<std::uintptr_t,std::size_t>,7> links{{{c,0x190},{c,0x1c8},{c,0x490},{m,0x3a8},{a,0x458},{a,0x168},{s,0x190}}};
    for(auto [owner,offset]:links){auto value=GuardPredicates::Read<std::uintptr_t>(owner,offset);Put(owner,offset,std::uintptr_t{0});if(guard.InspectContract(binding,after))return 4;Put(owner,offset,value);}
    Put(c,0x460,std::uintptr_t{1});if(guard.InspectContract(binding,after))return 5;Put(c,0x460,std::uintptr_t{});
    Put(w,0x1c8,std::uintptr_t{0x101});if(!guard.InspectContract(binding,after)||after==original)return 6;Put(w,0x1c8,std::uintptr_t{0x100});
    Put(c,8,0x18000u);if(guard.InspectContract(binding,after))return 7;Put(c,8,0u);
    auto* poison=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS);if(!poison)return 8;
    auto* handler=AddVectoredExceptionHandler(1,CountFault);if(!handler)return 9;
    binding.component.object=reinterpret_cast<std::uintptr_t>(poison);
    RC::Unreal::FUObjectArray::items[0]={poison,999,true};
    if(guard.InspectContract(binding,after)||faults)return 10;
    RemoveVectoredExceptionHandler(handler);VirtualFree(poison,0,MEM_RELEASE);
    return 0;
}
'''
    cpp=tmp_path/'consumer_guard.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:consumer-guard.exe /Fo:consumer-guard.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'consumer-guard.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,f'exit={checked.returncode}\n'+checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_consumer_host_retains_and_resumes_continuation(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS

    root = module.ROOT / "HorseMod/horselib/deterministic"
    source = (root / "Sc6ReplayHost.Consumer.inl").read_text(encoding="utf-8")
    start = source.index("bool Sc6ReplayHost::SuspendConsumerTask()")
    end = source.index("Status Sc6ReplayHost::ArmHistoricalConsumerGuard()", start)
    (tmp_path / "consumer_host_methods.inl").write_text(source[start:end], encoding="utf-8")
    application = (root / "Sc6ReplayHost.Application.inl").read_text(encoding="utf-8")
    start = application.index("void Sc6ReplayHost::TickApplication(void* loop)")
    end = application.index("    // This is outside every application/native stack.", start)
    # Keep the real strict-entry branch. Service/tail endpoints are observable
    # stubs, not replacements for its ownership decisions.
    prefix = application[start:end]
    prefix += "\n    ++self->service_calls; return;\nconsumer_application_tail:\n    ++self->tail_calls; self->application_active_=false;\n}\n"
    (tmp_path / "consumer_host_application_entry.inl").write_text(prefix, encoding="utf-8")
    world = (root / "Sc6ReplayWorld.cpp").read_text(encoding="utf-8")
    start = world.index("void Sc6ReplayWorld::AttachArena()")
    end = world.index("void Sc6ReplayWorld::CloseMemory()", start)
    (tmp_path / "consumer_host_arena.inl").write_text(world[start:end], encoding="utf-8")
    header = (root / "Sc6ReplayWorld.hpp").read_text(encoding="utf-8")
    start = header.index("    struct MemoryArena")
    end = header.index("    // Process-local leases.", start)
    (tmp_path / "consumer_host_arena_fields.inl").write_text(header[start:end], encoding="utf-8")
    start = header.index("    bool consumer_boundary(bool attached)")
    end = header.index("    bool RequestConsumerResume()", start)
    (tmp_path / "consumer_host_world_boundary.inl").write_text(header[start:end], encoding="utf-8")
    task_header = (root / "Sc6ReplayTaskGroup.hpp").read_text(encoding="utf-8")
    start = task_header.index("    bool at_consumer_boundary()")
    end = task_header.index("    void BindConsumerAdmission(", start)
    (tmp_path / "consumer_host_task_boundary.inl").write_text(task_header[start:end], encoding="utf-8")
    fixture = module.ROOT / "tools/replay_consumer_host_selftest.cpp"
    batch = tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:consumer-host.exe /Fo:consumer-host.obj\n', encoding="utf-8")
    built = run_compile(["cmd", "/d", "/c", str(batch)], cwd=tmp_path,
                           capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / "consumer-host.exe")], cwd=tmp_path,
                             capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, f"exit={checked.returncode}\n" + checked.stdout + checked.stderr
    assert "consumer host control-flow PASS" in checked.stdout



@pytest.mark.native_contract
def test_scheduler_observer_preserves_empty_object_memory_charge(tmp_path):
    """Actual observer predicates and storage accounting; no native restore claim."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    source=source[source.index('    bool PrepareCapturedScheduler('):]
    reject=source[source.index('capacity.code !='):source.index('|| prepared.storage_fingerprint()')].rstrip().rstrip('|').rstrip()
    commit=source[source.index('!applied.ok() || transaction.ready()'):source.index('|| !unchanged()',source.index('!applied.ok() || transaction.ready()'))].rstrip().rstrip('|').rstrip()
    storage=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.Storage.cpp').read_text()
    start=storage.index('std::size_t Sc6ReplaySchedulerState::PreparedRestore::owned_bytes()')
    accounting=storage[start:storage.index('Status Sc6ReplaySchedulerState::PreparedRestore::Allocate(',start)]
    fixture=r"""
#include <vector>
#include <algorithm>
#include <cstddef>
namespace Horse::Deterministic {enum class FailureCode{CapacityExceeded,Other};}
struct Status {Horse::Deterministic::FailureCode code;bool ok()const{return code==Horse::Deterministic::FailureCode::Other;}};
struct Sc6ReplaySchedulerState {struct PreparedRestore {
 struct Allocation{void* data;std::size_t charged;};struct Patch{std::size_t destination;};
 std::vector<Allocation> allocations_;std::vector<Patch> patches_;std::size_t execution_reservation_{};bool active{};
 bool ready()const{return active;}std::size_t patch_count()const{return patches_.size();}
 std::size_t owned_bytes()const noexcept;std::size_t allocation_count()const noexcept;
};};
using ReplayScheduler=Sc6ReplaySchedulerState;
"""+accounting+r"""
bool Reject(ReplayScheduler::PreparedRestore& rejected){Status capacity{Horse::Deterministic::FailureCode::CapacityExceeded};return """+reject+r""";}
bool Commit(ReplayScheduler::PreparedRestore& transaction){Status applied{Horse::Deterministic::FailureCode::Other};return """+commit+r""";}
int main(){
 ReplayScheduler::PreparedRestore p;
 if(p.owned_bytes()!=sizeof(p)||Reject(p)||Commit(p))return 81;
 p.active=true;if(!Reject(p)||!Commit(p))return 82;p.active=false;
 p.execution_reservation_=1;if(!Reject(p)||!Commit(p))return 83;p.execution_reservation_=0;
 p.allocations_.push_back({reinterpret_cast<void*>(1),8});if(!Reject(p)||!Commit(p))return 84;
 std::vector<ReplayScheduler::PreparedRestore::Allocation>().swap(p.allocations_);
 p.patches_.push_back({1});if(!Reject(p)||!Commit(p))return 85;
 return 0;
}
"""
    (tmp_path/'accounting.cpp').write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc accounting.cpp /Fe:accounting.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'accounting.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)



@pytest.mark.native_contract
def test_historical_observer_captures_current_values_for_retained_ticks(tmp_path):
    """Real Capture union and observer call; external memory/binding services only."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    observer=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    call=observer[observer.index('        auto status = current.Capture(base, world, budget'):].split(';',1)[0]+';'
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.cpp').read_text()
    start=source.index('Status Sc6ReplaySchedulerState::Capture(std::uintptr_t')
    method=source[start:source.index('std::uint64_t Sc6ReplaySchedulerState::storage_fingerprint()',start)]
    fixture=r"""
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <utility>
#include <map>
enum class FailureCode{IllegalTransition,GenerationMismatch,CapacityExceeded};
struct Status{bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
constexpr auto capacity=FailureCode::CapacityExceeded;
static std::map<std::uintptr_t,int> native_values;
template<class T> T Field(const std::array<std::byte,0x58>& a,std::size_t o){T v;std::memcpy(&v,a.data()+o,sizeof v);return v;}
template<class T>bool Read(std::uintptr_t,T&){return false;}
struct Sc6ReplaySchedulerState {
 struct RetainedPrimaryTicks {const void* context{};Status(*visit)(const void*,void*,bool(*)(void*,std::uintptr_t)){};};
 struct Tick{std::uintptr_t address;std::array<std::byte,0x58> binding_layout{};int value;};
 struct Level{std::uintptr_t address;};std::vector<Tick> ticks_;std::vector<Level> levels_;
 bool live=true;std::size_t owned_bytes()const{return sizeof(*this);}
 Status ValidateBindings(std::uintptr_t,void*)const{return {live};}
 Status CaptureTick(std::uintptr_t address,std::uintptr_t level,std::size_t){
  if(!native_values.contains(address))return Status::failure(FailureCode::GenerationMismatch);
  if(std::none_of(ticks_.begin(),ticks_.end(),[&](auto& t){return t.address==address;})){
   Tick t{address,{},native_values.at(address)};std::memcpy(t.binding_layout.data()+0x48,&level,sizeof level);ticks_.push_back(t);
  }return Status::success();
 }
 Status CaptureUnchecked(std::uintptr_t,void*,std::size_t budget){levels_.push_back({7});return CaptureTick(1,7,budget);}
 Status Capture(std::uintptr_t,void*,std::size_t,RetainedPrimaryTicks={},const Sc6ReplaySchedulerState* =nullptr)noexcept;
};
"""+method+r"""
Status Observe(Sc6ReplaySchedulerState& current,Sc6ReplaySchedulerState& target){std::uintptr_t base=1;void* world=reinterpret_cast<void*>(1);std::size_t budget=65536;
"""+call+r"""
return status;}
int main(){
 native_values={{1,10},{2,20}};Sc6ReplaySchedulerState target;
 target.levels_.push_back({7});target.CaptureTick(1,7,65536);target.CaptureTick(2,7,65536);
 native_values[1]=30;native_values[2]=40;
 Sc6ReplaySchedulerState current;
 if(!Observe(current,target).ok()||current.ticks_.size()!=2)return 81;
 if(current.ticks_[0].value!=30||current.ticks_[1].value!=40||target.ticks_[1].value!=20)return 82;
 target.live=false;Sc6ReplaySchedulerState rejected;
 if(Observe(rejected,target).ok()||!rejected.ticks_.empty())return 83;
 return 0;
}
"""
    (tmp_path/'current_capture.cpp').write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc current_capture.cpp /Fe:current-capture.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'current-capture.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,(checked.returncode,checked.stdout,checked.stderr)
