from pathlib import Path
from types import SimpleNamespace

import pytest

from tools.deterministic_qualification import replay_run, process_control
from tools.deterministic_qualification.artifacts import sha256_file
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.fixture(autouse=True)
def isolate_cli_admission_from_installed_game(monkeypatch):
    import replay_test
    monkeypatch.setattr(replay_test, 'live_preflight', lambda _: {'result':'ready','checks':[]})
    # CLI routing fixtures already replace build/capture boundaries; retaining
    # the real 126 MB checkout here adds no coverage. Source retention has its
    # own production-method tests, including native versus reporting changes.
    monkeypatch.setattr(replay_test, 'retain_sources', lambda *_: {'fixture': 'CLI admission only'})


def test_native_failed_receipt_stops_current_run_immediately(tmp_path):
    import ast,re
    from tools.deterministic_qualification import replay_control
    tree=ast.parse(Path(replay_control.__file__).read_text())
    guard=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='guard')
    end=next(i for i,n in enumerate(guard.body) if isinstance(n,ast.If)
             and ast.unparse(n.test)=="report['coherence_images_requested']")
    prefix=ast.Module(body=guard.body[:end],type_ignores=[])
    log=tmp_path/'run.log'
    scope=dict(exit_witness=SimpleNamespace(exit_code=lambda:None),
               process=SimpleNamespace(memory_info=lambda:SimpleNamespace(rss=1)),
               report={},args=SimpleNamespace(log=log),run_id='current',re=re,
               __package__=replay_control.__package__,diagnostic_log_state={})
    log.write_text('[ReplayQualification] run failed run_id=old reason=old_failure\n')
    exec(compile(prefix,'production_guard','exec'),scope)
    log.write_text('[ReplayQualification] run failed run_id=current reason=particle_owner_copy_failed\n')
    with pytest.raises(RuntimeError,match='particle_owner_copy_failed'):
        exec(compile(prefix,'production_guard','exec'),scope)


@pytest.mark.native_contract
def test_private_owner_drain_requires_fresh_gpu_completion(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    cpp=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text()
    header=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.hpp').read_text()
    start=cpp.index('bool Sc6ReplayParticleCopy::DrainPrivateOwnerRetirement()')
    end=cpp.index('\n}',start)+2
    drain=cpp[start:end].replace('Sc6ReplayParticleCopy::','')
    start=cpp.index('void Sc6ReplayParticleCopy::Poll()')
    end=cpp.index('    if (witness_.phase == Phase::ReadExecutionCoordinates',start)
    poll=cpp[start:end].replace('Sc6ReplayParticleCopy::','')+'}\n'
    start=header.index('    enum class Phase :')
    phase=header[start:header.index(';',start)+1]
    fixture=r'''
#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <cassert>
struct ReplayGpuCompletion {
 using Clock=std::chrono::steady_clock;
 enum class Result {Pending,Complete,TimedOut,Cancelled,Error};
 Result result=Result::Pending;bool in_flight=false;HRESULT submit_hr=S_OK;unsigned submissions=0;
 bool retired()const{return !in_flight;}
 HRESULT Submit(void*,Clock::time_point){++submissions;if(SUCCEEDED(submit_hr))in_flight=true;return submit_hr;}
 Result Poll(void*,Clock::time_point){if(result==Result::Complete)in_flight=false;return result;}
 HRESULT error()const{return S_OK;}
};
struct Copy {
 PHASE
 struct Witness {Phase phase=Phase::ReadyA;bool native_write_uncommitted=false,deadline_expired=false;
 bool timeout_retired=false,cancel_retired=false,execution_work_complete=false,undo_ready=false;
 std::uint64_t private_owner_retirements=0;} witness_;
 bool execution_started_=false,binding=true,prepare=true;std::uintptr_t world_=0;
 struct Context {void* Get(){return nullptr;}} context_;
 ReplayGpuCompletion completion_;ReplayGpuCompletion::Clock::time_point request_deadline_;
 Phase private_retirement_return_=Phase::Empty;
 bool Bindings(void*,bool){return binding;}bool PrepareTransfer(){return prepare;}
 void Fail(HRESULT){witness_.phase=completion_.retired()?Phase::Failed:Phase::RetiringFailure;}
 void StartRetirementProbe(bool){}
 DRAIN
 POLL
};
int main(){
 Copy c;assert(c.DrainPrivateOwnerRetirement());assert(c.completion_.in_flight);
 assert(!c.DrainPrivateOwnerRetirement() && c.completion_.submissions==1);
 c.Poll();assert(!c.witness_.private_owner_retirements && c.witness_.phase==Copy::Phase::ReadPrivateOwnerRetirement);
 c.completion_.result=ReplayGpuCompletion::Result::Complete;c.Poll();
 assert(c.witness_.private_owner_retirements==1 && c.witness_.phase==Copy::Phase::ReadyA);
 c.Poll();assert(c.witness_.private_owner_retirements==1);
 c.witness_.phase=Copy::Phase::UndoReady;assert(c.DrainPrivateOwnerRetirement());
 assert(c.witness_.private_owner_retirements==1);c.Poll();
 assert(c.witness_.private_owner_retirements==2 && c.witness_.phase==Copy::Phase::UndoReady);
 Copy rejected;rejected.witness_.phase=Copy::Phase::Failed;
 assert(!rejected.DrainPrivateOwnerRetirement());
 rejected.witness_.undo_ready=true;assert(rejected.DrainPrivateOwnerRetirement());
 rejected.completion_.result=ReplayGpuCompletion::Result::Complete;rejected.Poll();
 assert(rejected.witness_.phase==Copy::Phase::Failed && rejected.witness_.private_owner_retirements==1);
 for(auto result:{ReplayGpuCompletion::Result::TimedOut,ReplayGpuCompletion::Result::Cancelled,ReplayGpuCompletion::Result::Error}){
  Copy f;assert(f.DrainPrivateOwnerRetirement());f.completion_.result=result;f.Poll();
  assert(f.completion_.in_flight && !f.witness_.private_owner_retirements && f.witness_.phase==Copy::Phase::RetiringFailure);
  f.completion_.result=ReplayGpuCompletion::Result::Complete;f.Poll();assert(!f.witness_.private_owner_retirements);
 }
 for(int i=0;i<7;++i){Copy f;
  if(i==0)f.execution_started_=true;if(i==1)f.witness_.native_write_uncommitted=true;
  if(i==2)f.completion_.in_flight=true;if(i==3)f.witness_.phase=Copy::Phase::Installed;
  if(i==4)f.binding=false;if(i==5)f.prepare=false;if(i==6)f.witness_.private_owner_retirements=UINT64_MAX;
  assert(!f.DrainPrivateOwnerRetirement() && !f.completion_.submissions);
 }
 Copy recovered;recovered.witness_.phase=Copy::Phase::Recovered;
 assert(recovered.DrainPrivateOwnerRetirement());
 assert(recovered.witness_.phase==Copy::Phase::ReadPrivateOwnerRetirement);
 recovered.completion_.result=ReplayGpuCompletion::Result::Complete;recovered.Poll();
 assert(recovered.witness_.phase==Copy::Phase::Recovered && recovered.witness_.private_owner_retirements==1);
 for(int i=0;i<3;++i){Copy f;f.witness_.phase=Copy::Phase::Recovered;
  if(i==0)f.execution_started_=true;if(i==1)f.witness_.native_write_uncommitted=true;if(i==2)f.completion_.in_flight=true;
  assert(!f.DrainPrivateOwnerRetirement() && !f.completion_.submissions);
 }
 Copy failed;failed.completion_.submit_hr=E_FAIL;assert(!failed.DrainPrivateOwnerRetirement());
 assert(!failed.witness_.private_owner_retirements && failed.witness_.phase==Copy::Phase::Failed);
}
'''.replace('PHASE',phase).replace('DRAIN',drain).replace('POLL',poll)
    (tmp_path/'drain.cpp').write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc drain.cpp /Fe:drain.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'drain.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_particle_gpu_owner_requires_completed_retirement_receipt():
    from replay_test import validate_particle_gpu_owner
    receipt=('particle GPU owner run_id=c tick=205 component=100 root=200 render=300 charged_bytes=1400 '
             'completion_serial=1 constructed=true native_retirement_returned=true gpu_completed=true '
             'graph_published=false rollback_proof=false')
    receipt='[ReplayQualification] '+receipt
    assert validate_particle_gpu_owner(receipt,'c')['result']=='pass'
    for bad in ('',receipt+'\n'+receipt,receipt.replace('gpu_completed=true','gpu_completed=false'),
                receipt.replace('completion_serial=1','completion_serial=0'),receipt.replace('root=200','root=100'),
                receipt.replace('charged_bytes=1400','charged_bytes=0'),receipt.replace('tick=205','tick=206'),
                receipt.replace('graph_published=false','graph_published=true'),receipt.replace('run_id=c','run_id=old')):
        with pytest.raises(RuntimeError):validate_particle_gpu_owner(bad,'c')


def test_particle_owner_copy_requires_complete_scoped_receipt():
    from replay_test import validate_particle_owner_copy
    receipt = ("[ReplayQualification] particle owner copy run_id=c tick=205 check=cold_copy_retired "
               "source=0x100 copy=0x200 outer=0x300 properties=20 arrays=2 outer_members=3 "
               "created=true detached=true destroyed=true lease_released=true source_unchanged=true "
               "registration_performed=false activation_performed=false rollback_proof=false")
    result = validate_particle_owner_copy(receipt, "c")
    assert result["result"] == "pass" and result["rollback_proof"] is False
    registered=receipt.replace('check=cold_copy_retired','check=inactive_copy_retired').replace('detached=true','detached=false').replace('registration_performed=false','registration_performed=true')
    assert validate_particle_owner_copy(registered,'c',registration=True)['result']=='pass'
    with pytest.raises(RuntimeError): validate_particle_owner_copy(registered,'c')
    with pytest.raises(RuntimeError): validate_particle_owner_copy(receipt,'c',registration=True)
    for field in ("created", "detached", "destroyed", "lease_released", "source_unchanged"):
        with pytest.raises(RuntimeError):
            validate_particle_owner_copy(receipt.replace(field+"=true", field+"=false"), "c")
    for bad in ("", receipt+"\n"+receipt, receipt.replace("run_id=c", "run_id=other"),
                receipt.replace("copy=0x200", "copy=0x100"), receipt.replace("tick=205", "tick=206"),
                receipt.replace("copy=0x200", "copy=0"), receipt.replace("outer=0x300", "outer=invalid"),
                receipt.replace("rollback_proof=false", "rollback_proof=true"),
                receipt.replace("arrays=2", "arrays=0")):
        with pytest.raises(RuntimeError): validate_particle_owner_copy(bad, "c")


@pytest.mark.native_contract
def test_particle_copy_failed_hold_survives_repeated_failure(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('    void Fail(std::string_view reason,bool preserve_native_hold=false)')
    end=source.index('        if (!UndoPreparedEmitters()',start)
    prefix=source[start:end]
    fixture='''#include <string_view>
#include <string>
#include <cassert>
#include <cstdio>
#include <type_traits>
#define STR(x) x
enum class LogLevel {Warning};
struct Output {template<LogLevel L,class... T> static void send(T...) {}};
namespace RC {template<class T> auto to_generic_string(T x) {return x;}}
// External export service only; the Fail ordering and hold decision are the
// extracted production prefix, not a fixture implementation of invalidation.
static bool export_available{};
static unsigned resolutions{},invalidations{};
static std::string invalidated_run;
static void Invalidate(const char* run) {assert(run);++invalidations;invalidated_run=run;}
template<class T>static T ResolveHorseModExport(const char* name) {
 static_assert(std::is_same_v<T,void(*)(const char*)>);
 assert(std::string_view(name)=="horsemod_invalidate_collection18_combat_observation");
 ++resolutions;return export_available?&Invalidate:nullptr;
}
struct Probe {
 enum class State {Running,Failed}; State state_{State::Running};
 struct {std::string run_id="held-run";bool particle_owner_copy=true,c18_diagnostic=false;} request_;
 unsigned cleanup{};
'''+prefix+''' ++cleanup;
 }
};
int main() {
 for(bool diagnostic:{false,true})for(bool available:{false,true}) {
 export_available=available;resolutions=invalidations=0;invalidated_run.clear();
 Probe failed; failed.request_.c18_diagnostic=diagnostic;
 failed.Fail("partial",true);
 if(failed.state_!=Probe::State::Failed || failed.cleanup)return 1;
 if(resolutions!=unsigned(diagnostic) || invalidations!=unsigned(diagnostic&&available))return 5;
 failed.Fail("later callback");
 if(failed.state_!=Probe::State::Failed || failed.cleanup)return 2;
 // Invalidation precedes the repeated held-owner early return on both calls.
 if(resolutions!=2u*diagnostic || invalidations!=2u*(diagnostic&&available))return 6;
 if(invalidated_run!=(diagnostic&&available?"held-run":""))return 7;
 }
 export_available=true;resolutions=invalidations=0;
 Probe ordinary; ordinary.request_.particle_owner_copy=false;
 ordinary.Fail("ordinary"); if(ordinary.cleanup!=1)return 3;
 Probe rejected; rejected.Fail("preflight"); if(rejected.cleanup!=1)return 4;
 if(resolutions || invalidations)return 8; // opt-out does not even resolve
 Probe diagnostic; diagnostic.request_.particle_owner_copy=false;
 diagnostic.request_.c18_diagnostic=true;diagnostic.request_.run_id="combat-run";
 diagnostic.Fail("combat scene");
 if(diagnostic.state_!=Probe::State::Failed || diagnostic.cleanup!=1
    || resolutions!=1 || invalidations!=1 || invalidated_run!="combat-run")return 9;
 std::puts("production Fail PASS: hold_preserved=1 repeated_invalidation=1 opt_out=1 missing_export=1 ordinary_cleanup=1 run_id_forwarded=1");
}
'''
    (tmp_path/'held.cpp').write_text(fixture)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc held.cpp /Fe:held.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'held.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    (tmp_path/'held.stdout.log').write_text(checked.stdout+checked.stderr)
    assert checked.returncode==0, f'production Fail escaped held failure: {checked.returncode}\n{checked.stdout}{checked.stderr}'


@pytest.mark.native_contract
def test_particle_copy_recursive_owned_allocation_bounds(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayColdParticleOwner.hpp').read_text(encoding='utf-8')
    start=source.index('    static bool BoundedProperty(')
    end=source.index('    static bool Cold(',start)
    (tmp_path/'particle_copy_bounds.inl').write_text(source[start:end])
    start=source.index('    static bool SingleGpuSource(')
    end=source.index('    static bool ConstructDetached(',start)
    (tmp_path/'particle_copy_single_gpu.inl').write_text(source[start:end])
    start=source.index('    static bool PassiveOuterModify(')
    end=source.index('    static bool SingleGpuSource(',start)
    (tmp_path/'particle_copy_outer_modify.inl').write_text(source[start:end])
    start=source.index('    static bool EquivalentDetached(')
    end=source.index('#include "Sc6ReplayColdParticleMaterials.inl"',start)
    (tmp_path/'particle_copy_equivalence.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_particle_copy_bounds_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:bounds.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'bounds.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,f'production recursive ownership boundary failed: {checked.returncode}'


@pytest.mark.native_contract
def test_particle_copy_uses_origin_module_validation(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayObjectLease.cpp').read_text()
    start=source.index('    bool Membership() const noexcept')
    end=source.index('    Status MutateRegistry(',start)
    (tmp_path/'lease_membership.inl').write_text(source[start:end])
    probe=(module.ROOT/'tools/replay_qualification_mod/ReplayParticleOwnerCopy.hpp').read_text()
    start=probe.index('        const auto fail=')
    end=probe.index('        const auto manager=',start)
    (tmp_path/'copy_source_validation.inl').write_text(probe[start:end])
    fixture=module.ROOT/'tools/replay_particle_copy_module_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /LD /DOWNER_MODULE "{fixture}" /Fe:owner.dll /Fo:owner.obj\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:observer.exe /Fo:observer.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'observer.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,f'production cross-module validation failed: {checked.returncode}'


def test_unchanged_commit_checks_particle_seeds_before_control():
    import ast
    import types
    import replay_test as module
    source=module.Path(module.__file__).read_text(encoding='utf-8')
    tree=ast.parse(source)
    guard=min((node for node in ast.walk(tree) if isinstance(node,ast.If)
        and any(isinstance(call,ast.Call) and isinstance(call.func,ast.Name)
            and call.func.id=='compare_repeated_particle_seeds' for child in node.body for call in ast.walk(child))),key=lambda node:node.end_lineno-node.lineno)
    options=types.SimpleNamespace(host_seek=True,host_seek_repeat=False,changed_inputs=False,
        corrected_inputs=False,combat_anchor_tick=210,host_seek_target=217,combat_case='commit')
    assert eval(compile(ast.Expression(guard.test),'<production guard>','eval'),{'options':options,'first_case':''})


@pytest.mark.native_contract
def test_foot_contact_history_restores_without_duplicate_landing(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    header=(root/'Sc6ReplayWorldState.hpp').read_text(encoding='utf-8')
    a=header.index('    struct Values\n');b=header.index('    } values_;',a)
    (tmp_path/'foot_values.inl').write_text(header[a:b]+'    };',encoding='utf-8')
    source=(root/'Sc6ReplayWorldState.cpp').read_text(encoding='utf-8')
    pieces=[]
    for start,end in [('Status Sc6ReplayWorldState::Read(', 'Status Sc6ReplayWorldState::ValidateRecord('),
            ('bool Sc6ReplayWorldState::Write(', 'Status Sc6ReplayWorldState::Restore(')]:
        a=source.index(start);pieces.append(source[a:source.index(end,a)])
    (tmp_path/'foot_read_write.inl').write_text('\n'.join(pieces),encoding='utf-8')
    fixture=module.ROOT/'tools/replay_foot_history_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:foot.exe /Fo:foot.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'foot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_independent_particle_lifecycle_rejects_missing_and_shifted_events(tmp_path):
    from replay_test import compare_particle_lifecycle
    start='[ReplayQualification] boundary ordinal=1 phase=input_cache_publication sample_version=3 frame=210 round=0\n'
    end='[ReplayQualification] trajectory ordinal=2 phase=engine_post sample_version=3 frame=217 round=0\n'
    events=('[ReplayQualification] particle GPU initialized run_id=n tick=214 component=ab emitter=cd template=Guard\n'
        '[ReplayQualification] particle seed run_id=n tick=214 game_thread=true initial=1 current=2\n'
        '[ReplayQualification] particle retirement run_id=n tick=217 component=ef emitter=ff slot=9 vtable_rva=394c100 template=Hit native_completed=true peer_notifications_and_destructor_returned=true slot_cleared=true\n')
    n=tmp_path/'n.log';c=tmp_path/'c.log';n.write_text(start+events+end,encoding='utf-8')
    native={'run_id':'n','raw_log':{'path':str(n)}};candidate={'run_id':'c','raw_log':{'path':str(c)}}
    rows=events.replace('run_id=n','run_id=c').replace('emitter=cd','emitter=aa')
    c.write_text(start+rows+end,encoding='utf-8');assert compare_particle_lifecycle(native,candidate,210,217)['result']=='pass'
    for changed in (rows.replace('tick=217','tick=216'),rows.splitlines(True)[0]+rows.splitlines(True)[1],rows+rows,rows.replace('initial=1','initial=3')):
        c.write_text(start+changed+end,encoding='utf-8');assert compare_particle_lifecycle(native,candidate,210,217)['result']=='fail'
    c.write_text(start+rows.replace('slot_cleared=true','slot_cleared=false')+end,encoding='utf-8')
    with pytest.raises(RuntimeError,match='retirement incomplete'):compare_particle_lifecycle(native,candidate,210,217)


@pytest.mark.native_contract
def test_correction_admission_keeps_native_ticks_distinct_from_samples(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=module.ROOT/'tools/replay_correction_admission_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT}" /I"{module.BUILD / "HorseMod/generated"}" "{fixture}" /Fe:admission.exe /Fo:admission.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'admission.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_rolling_schedule_is_owned_before_first_forward_tick(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Rolling.inl').read_text(encoding='utf-8')
    start=source.index('bool Sc6ReplayHost::BeginRollingSchedule(')
    end=source.index('\n}\n',start)+3
    (tmp_path/'rolling_schedule_start.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_rolling_schedule_start_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT}" /I"{module.BUILD / "HorseMod/generated"}" /I. "{fixture}" /Fe:schedule.exe /Fo:schedule.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'schedule.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_corrected_capture_preserves_complete_b_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Checkpoint.inl').read_text(encoding='utf-8')
    start=source.index('void Sc6ReplayHost::AdvanceCaptureOperation()')
    helper='std::unique_ptr<Sc6ReplayParticleCopy>& Sc6ReplayHost::CaptureParticleCopyOwner()'
    if helper in source: start=source.index(helper)
    (tmp_path/'corrected_capture.inl').write_text(source[start:],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_corrected_capture_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:capture.exe /Fo:capture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'capture.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_rolling_window_turnover_preserves_operation_pins(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Rolling.inl').read_text(encoding='utf-8')
    a=source.index('bool Sc6ReplayHost::StoreRollingCheckpoint(');b=source.index('bool Sc6ReplayHost::AdvanceRollingForward(',a)
    (tmp_path/'rolling_window.inl').write_text(source[a:b],encoding='utf-8')
    start=source.index('        w.current=held.tick;')
    end=source.index('        w.phase=RollingPhase::Capturing;return false;',start)+len('        w.phase=RollingPhase::Capturing;return false;')
    (tmp_path/'rolling_capture_entry.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_rolling_window_selftest.cpp'
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT}" "{fixture}" /Fe:rolling.exe /Fo:rolling.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'rolling.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_rolling_receipts_use_selected_initial_boundary(tmp_path):
    from replay_test import validate_rolling_candidate
    from deterministic_qualification.replay_control import expected_historical_rewinds
    path=tmp_path/'rolling332.log'
    rows=[f'[HorseMod] rolling cycle ordinal={i+1} T={339+i} A={332+i} resimulated_ticks=7 committed=true tails_completed=true retirement_complete=true\n' for i in range(30)]
    end='[ReplayQualification] rolling completed run_id=c first=332 last=368 cycles=30 resimulated_ticks=210 resimulated_intervals=210 peak_bytes=100 missed=0\n'
    path.write_text(''.join(rows)+end,encoding='utf-8')
    report={'run_id':'c','rolling_cycles':30,'historical_anchor_tick':332,'raw_log':{'path':str(path)}}
    result=validate_rolling_candidate(report,30)
    assert (result['first_T'],result['last_T'])==(339,368)
    assert expected_historical_rewinds(report)==[(339+i,332+i) for i in range(30)]
    report['historical_anchor_tick']=210
    with pytest.raises(RuntimeError):validate_rolling_candidate(report,30)


def test_rolling_motion_probe_requires_real_complete_intervention(tmp_path):
    from replay_test import validate_rolling_candidate
    path=tmp_path/'motion.log'
    rows=''.join(f'[HorseMod] rolling cycle ordinal={i+1} T={217+i} A={210+i} resimulated_ticks=7 committed=true tails_completed=true retirement_complete=true\n' for i in range(407))
    end='[ReplayQualification] rolling completed run_id=c first=210 last=623 cycles=407 resimulated_ticks=2849 resimulated_intervals=2849 peak_bytes=100 missed=0\n'
    proof='[ReplayQualification] ground motion perturbed run_id=c tick=623 roots=1 meshes=5 changed=5 owned_bytes=4096 native_move=true logical_unchanged=true rng_unchanged=true before_resume=true\n'
    report={'run_id':'c','rolling_cycles':407,'ground_motion_perturb':True,'raw_log':{'path':str(path)}}
    for receipt in ['',proof.replace('changed=5','changed=4'),proof.replace('rng_unchanged=true','rng_unchanged=false'),proof.replace('tick=623','tick=622'),proof+proof]:
        path.write_text(rows+end+receipt)
        with pytest.raises(RuntimeError,match='ground motion'):validate_rolling_candidate(report,407)
    path.write_text(rows+end+proof);assert validate_rolling_candidate(report,407)['motion_probe']['changed']==5
    report['ground_motion_perturb']=False
    with pytest.raises(RuntimeError,match='ground motion'):validate_rolling_candidate(report,407)


def test_rolling_receipts_require_every_exact_cycle(tmp_path):
    from replay_test import validate_rolling_candidate
    from deterministic_qualification.replay_control import expected_historical_rewinds
    rows=[f'[HorseMod] rolling cycle ordinal={i+1} T={217+i} A={210+i} resimulated_ticks=7 committed=true tails_completed=true retirement_complete=true\n' for i in range(30)]
    end='[ReplayQualification] rolling completed run_id=c first=210 last=246 cycles=30 resimulated_ticks=210 resimulated_intervals=210 peak_bytes=100 missed=0\n'
    p=tmp_path/'rolling.log';report={'run_id':'c','rolling_cycles':30,'raw_log':{'path':str(p)}}
    assert expected_historical_rewinds(report)==[(217+i,210+i) for i in range(30)]
    p.write_text(''.join(rows)+end,encoding='utf-8');assert validate_rolling_candidate(report,30)['cycles']==30
    for changed in (rows[:-1],rows[:10]+rows[11:],rows[:1]*30,[r.replace('A=220','A=219') for r in rows]):
        p.write_text(''.join(changed)+end,encoding='utf-8')
        with pytest.raises(RuntimeError):validate_rolling_candidate(report,30)


def test_repeated_particle_seeds_compare_every_native_draw():
    from replay_test import compare_repeated_particle_seeds
    start='[ReplayQualification] boundary ordinal=1 phase=input_cache_publication sample_version=3 frame=210 round=0\n'
    end='[ReplayQualification] trajectory ordinal=2 phase=engine_post sample_version=3 frame=217 round=0\n'
    seed='[ReplayQualification] particle seed run_id=c tick=214 game_thread=true initial=16962 current=2188845845\n'
    native=start+seed+end
    assert compare_repeated_particle_seeds(native+native,'c',210,217)['result']=='pass'
    for changed in (seed.replace('16962','14579'),seed+seed,'',seed.replace('tick=214','tick=210')):
        result=compare_repeated_particle_seeds(native+start+changed+end,'c',210,217)
        assert result['result']=='fail'
    with pytest.raises(RuntimeError,match='foreign thread'):
        compare_repeated_particle_seeds(native+native.replace('game_thread=true','game_thread=false'),'c',210,217)
    with pytest.raises(RuntimeError,match='two native traversal'):
        compare_repeated_particle_seeds(native,'c',210,217)
    with pytest.raises(RuntimeError,match='completed traversal'):
        compare_repeated_particle_seeds(native+start+seed,'c',210,217)


def test_historical_report_preserves_first_checkpoint_lifetime_rejection():
    """Execute the production final-report block, including its real ordering."""
    import ast
    import re
    from tools.deterministic_qualification import replay_control
    tree = ast.parse(Path(replay_control.__file__).read_text(encoding="utf-8"))
    function = next(node for node in tree.body if isinstance(node, ast.FunctionDef)
                    and node.name == "run_replay_control")
    block = next(node for node in ast.walk(function) if isinstance(node, ast.If)
                 and ast.unparse(node.test) == "report['probe_historical_restore']"
                 and any(isinstance(child, ast.Assign)
                         and any(ast.unparse(target) == "report['historical_milestone_complete']"
                                 for target in child.targets) for child in node.body))
    text = ("[HorseMod] checkpoint lease failure tick=6417 reference=352 "
            "pending_kill=true owner=LuxParticleSystemComponent /Game/Stage/component_878\n"
            "[HorseMod] historical restore preflight phase=late code=7\n")
    scope = dict(raw=text.encode(), report={"probe_historical_restore": True}, re=re)
    exec(compile(ast.Module(body=[block], type_ignores=[]), "production_failure_report", "exec"), scope)
    failure = scope["report"]["first_mechanism_failure"]
    assert failure["tick"] == "6417"
    assert failure["failure_kind"] == "checkpoint lease failure"
    assert failure["pending_kill"] == "true"
    assert failure["owner"] == "LuxParticleSystemComponent /Game/Stage/component_878"
    assert not scope["report"]["historical_milestone_complete"]


@pytest.mark.parametrize("second",[(170,220,True),(170,208,True),(205,209,False)])
def test_topology_receipts_track_transactions_with_repeated_coordinates(second):
    from replay_test import validate_topology_admissions
    expected=[(170,220,True),second]
    lines=[f"historical topology admitted run_id=c target_tick={a} original_tick={b} particle_birth={str(birth).lower()}" for a,b,birth in expected]
    validate_topology_admissions("\n".join(lines),"c",expected)
    for bad in (lines[:1],lines+lines[:1],[line.replace("particle_birth=true","particle_birth=invalid") for line in lines]):
        with pytest.raises(RuntimeError):validate_topology_admissions("\n".join(bad),"c",expected)
    if expected[0]!=second:
        with pytest.raises(RuntimeError):validate_topology_admissions("\n".join(reversed(lines)),"c",expected)
    validate_topology_admissions("","c",[])
    with pytest.raises(RuntimeError):validate_topology_admissions(lines[0],"c",[])


def test_native_control_reuse_requires_identical_interventions(tmp_path, monkeypatch):
    import json
    import replay_test
    raw=tmp_path / "native.log"
    raw.write_text("native evidence",encoding="utf-8")
    path=tmp_path / "native.json"
    identities=dict(runtime="runtime",observer="observer",framework="framework",replay="replay")
    report=dict(result="captured",mode="stock",run_id="native",cleanup=dict(complete=True,games_remaining=0),
        observations_requested=120,full_match=True,include_setup=True,record_index=True,
        skip_intros=True,native_fixed_seed=False,serial_particles=False,completion=dict(full_match=True),
        identities=identities,raw_log=dict(path=str(raw),sha256=sha256_file(raw)),
        loaded_runtime=dict(verification="owned_process_runtime_absent",pid=7,process_created="created"))
    for key in ("observer","framework"):
        report["loaded_"+key]=dict(sha256=identities[key],pid=7,process_created="created")
    args=SimpleNamespace(report=path,watch_frames=120,full_match=True,include_setup=True,record_index=True,
        skip_intros=True,native_fixed_seed=False,serial_particles=False)
    monkeypatch.setattr(replay_test,"current_identities",lambda _:identities)
    validated=[]
    monkeypatch.setattr(replay_test,"validate_intro_skips",lambda text,run:validated.append((text,run)))
    path.write_text(json.dumps(report),encoding="utf-8")
    replay_test.require_resumable_baseline(args)
    assert validated==[("native evidence","native")]
    for field in ("skip_intros","native_fixed_seed","serial_particles"):
        changed=dict(report);changed[field]=not report[field]
        path.write_text(json.dumps(changed),encoding="utf-8")
        with pytest.raises(RuntimeError,match="incomplete or stale"):
            replay_test.require_resumable_baseline(args)


def test_intro_skip_without_setup_rejects_before_paths_or_deployment():
    from tools.deterministic_qualification.replay_control import run_replay_control
    # No file/process arguments exist: admission must fail before touching them.
    with pytest.raises(RuntimeError, match="requires setup observation; deployment was not started"):
        run_replay_control(SimpleNamespace(watch_frames=120,skip_intros=True))


@pytest.mark.parametrize("origin", [220,2504])
@pytest.mark.parametrize("failure", [False,True])
def test_capture_boundary_plan_uses_requested_original(origin,failure):
    from tools.deterministic_qualification.replay_control import expected_historical_rewinds
    report=dict(historical_anchor_tick=170,historical_advanced_tick=origin,
        host_seek_target=214,host_seek_first_target=0,host_seek_repeat=False,
        historical_cancel="after",seek_advance_failure=failure,checkpoint_pair=False)
    assert expected_historical_rewinds(report)==([(origin,170)] if failure else [])
    report["historical_cancel"]=""
    assert expected_historical_rewinds(report)==[(origin,170)]


@pytest.mark.parametrize("original", [220,2504])
def test_seek_recovery_requires_native_C_tails_birth_retirement_and_original_B(original):
    import replay_test
    rows = [("BRetained",original,0,0,0),("APublished",170,0,0,0),("ExecutionActive",170,0,0,0),
            ("RecoveryQuiescing",208,0,0,0),("Recovering",209,209,39,39),("Recovered",original,209,39,39)]
    lines=[]
    for state,current,tail,ticks,intervals in rows:
        lines.append(f"seek ownership state={state} B_tick={original} target=214 current_tick={current} completed_tail_tick={tail} "
                     f"B_retained={'false' if state=='Recovered' else 'true'} commit_decided=false executed_ticks={ticks} executed_intervals={intervals}")
        if state=="RecoveryQuiescing":
            lines.append("seek C-only retirement owners=3 completed=3 B_retained=true native_render_drain_required=true")
    raw="\n".join(lines)
    result=replay_test.validate_seek_recovery_ownership(raw,170,original,208,214)
    assert result["completed_C_tick"]==209 and result["B_recovered_tick"]==original and result["executed_ticks"]==39
    for bad in (raw.replace("state=ExecutionActive", "state=Committed"),
                raw.replace(f"current_tick={original} completed_tail_tick=209", "current_tick=209 completed_tail_tick=209"),
                raw.replace("executed_ticks=39", "executed_ticks=38"),
                raw.replace("owners=3 completed=3", "owners=3 completed=2"),
                raw.replace("owners=3 completed=3", "owners=0 completed=0"),
                raw.replace("state=APublished", "state=CSettled"),
                raw.replace("commit_decided=false", "commit_decided=true"),
                raw+"\n"+lines[2]):
        with pytest.raises(RuntimeError): replay_test.validate_seek_recovery_ownership(bad,170,original,208,214)


@pytest.mark.parametrize("original", [220,2504])
def test_late_settlement_recovery_requires_reopening_and_no_commit(original):
    import replay_test
    rows=[("BRetained",original,0,0),("APublished",170,0,0),("ExecutionActive",170,0,0),
          ("CSettled",214,0,0),("CompletingTargetTails",214,0,0),("FailedRecoverable",214,0,0),
          ("RecoveryQuiescing",214,0,0),("Recovering",214,214,44),("Recovered",original,214,44)]
    lines=[]
    for state,current,tail,ticks in rows:
        lines.append(f"seek ownership state={state} B_tick={original} target=214 current_tick={current} completed_tail_tick={tail} "
                     f"B_retained={'false' if state=='Recovered' else 'true'} commit_decided=false executed_ticks={ticks} executed_intervals={ticks}")
        if state=="RecoveryQuiescing":
            lines.extend(["seek CPU settlement reopened B_retained=true native_allocations_freed=0",
                          "seek C-only retirement owners=7 completed=7 B_retained=true native_render_drain_required=true"])
    raw="\n".join(lines)
    assert replay_test.validate_seek_recovery_ownership(raw,170,original,214,214,True)["B_recovered_tick"]==original
    for bad in (raw.replace("seek CPU settlement reopened", "missing reopening"),
                raw.replace("state=FailedRecoverable", "state=Committed"),
                raw.replace("native_allocations_freed=0", "native_allocations_freed=1"),
                raw.replace("owners=7 completed=7", "owners=7 completed=6")):
        with pytest.raises(RuntimeError): replay_test.validate_seek_recovery_ownership(bad,170,original,214,214,True)


def test_combat_seek_readiness_cannot_qualify_completed_cost():
    import replay_test
    assert replay_test.combat_seek_performance(None,True,True)["result"]=="not_measured"
    assert replay_test.combat_seek_performance({"elapsed_us":651000},True,True)["result"]=="fail"
    assert replay_test.combat_seek_performance({"elapsed_us":499000},True,True)["result"]=="pass"
    assert replay_test.combat_seek_performance({"elapsed_us":499000},True,True,True)["result"]=="diagnostic_only"


def test_native_emitter_recovery_requires_destruction_and_complete_B():
    import replay_test
    rows=[("BRetained",210,0,0),("APublished",205,0,0),("ExecutionActive",205,0,0),
          ("CSettled",208,0,0),("CompletingTargetTails",208,0,0),("FailedRecoverable",208,0,0),
          ("RecoveryQuiescing",208,0,0),("Recovering",208,208,3),("Recovered",210,208,3)]
    lines=["emitter native retirement settled kind=cpu component=1234 ordinal=1 destructor_completed=true slot_null=true B_retained=true C_freed_by_native=true"]
    for state,current,tail,ticks in rows:
        lines.append(f"seek ownership state={state} B_tick=210 target=208 current_tick={current} completed_tail_tick={tail} "
                     f"B_retained={'false' if state=='Recovered' else 'true'} commit_decided=false executed_ticks={ticks} executed_intervals={ticks}")
        if state=="RecoveryQuiescing":
            lines.extend(["seek CPU settlement reopened B_retained=true native_allocations_freed=0",
                          "seek C-only retirement owners=0 completed=0 B_retained=true native_render_drain_required=true"])
    raw="\n".join(lines)
    result=replay_test.validate_seek_recovery_ownership(raw,205,210,208,208,True)
    assert result["native_destroyed_CPU_members"]==1 and result["B_recovered_tick"]==210
    for old,new in [("destructor_completed=true","destructor_completed=false"),("slot_null=true","slot_null=false"),
                    ("C_freed_by_native=true","C_freed_by_native=false"),("state=Recovered B_tick=210 target=208 current_tick=210","state=Recovered B_tick=210 target=208 current_tick=208"),
                    ("native_allocations_freed=0","native_allocations_freed=1")]:
        with pytest.raises(RuntimeError):replay_test.validate_seek_recovery_ownership(raw.replace(old,new),205,210,208,208,True)


def test_recovery_controls_require_real_input_after_B_and_host_dispatch():
    import replay_test
    lines=["replay cancel control hwnd=2 screen_x=10 screen_y=20",
           "replay resume control hwnd=3 screen_x=30 screen_y=40",
           "historical cancel control waiting run_id=r tick=214",
           "replay cancel activated request=1 surface_frame=20",
           "replay UI cancel dispatched tick=214 accepted=true phase=9",
           "historical cancellation recovered run_id=r point=after tick=220",
           "historical resume control waiting run_id=r tick=220 recovered_B=true",
           "replay resume activated request=1 surface_frame=30",
           "historical resume control verified run_id=r tick=220 requests=1 recovered_B=true",
           "replay UI resume dispatched tick=220 seek_released=true",
           "seek checkpoint retired tick=220 application_idle=true"]
    report={"run_id":"r","loaded_observer":{"pid":7}}
    for action,hwnd,x,y in (("cancel",2,10,20),("resume",3,30,40)):
        report[f"ui_{action}_input"]={"pid":7,"method":"Windows SendInput","button_released":True,"hwnd":hwnd,"x":x,"y":y}
    raw="\n".join(lines)
    assert replay_test.validate_recovery_controls(raw,report)["result"]=="pass"
    for bad in (raw.replace("accepted=true","accepted=false"),raw.replace("seek_released=true","seek_released=false"),
                raw+"\nreplay resume activated request=2 surface_frame=31", "\n".join(lines[:5]+lines[7:8]+lines[5:7]+lines[8:])):
        with pytest.raises(RuntimeError):replay_test.validate_recovery_controls(bad,report)
    current="\n".join(lines[:-2]+[lines[-1],lines[-2]+" seek_release_result=completed"])
    assert replay_test.validate_recovery_controls(current,report)["result"]=="pass"
    with pytest.raises(RuntimeError):replay_test.validate_recovery_controls(raw.replace("seek_released=true","seek_released=true seek_release_result=completed"),report)
    report["ui_resume_input"]["pid"]=8
    with pytest.raises(RuntimeError):replay_test.validate_recovery_controls(raw,report)


def test_stage_reuses_build_only_after_complete_provenance_validation(monkeypatch):
    import replay_test
    calls=[]
    tested=[]
    monkeypatch.setattr(replay_test,"run_local_regressions",lambda proven:tested.append(proven))
    monkeypatch.setattr(replay_test,"build",lambda jobs:calls.append(jobs) or {"rebuilt":True})
    def valid(root, build, binaries):
        assert set(binaries)=={"runtime","observer","framework","core_test","native_test"}
        return {"verified":True}
    monkeypatch.setattr(replay_test,"require_build_provenance",valid)
    assert replay_test.ensure_build(4)=={"verified":True} and not calls
    def changed(*args): raise RuntimeError("retained build input changed")
    monkeypatch.setattr(replay_test,"require_build_provenance",changed)
    assert replay_test.ensure_build(4)=={"rebuilt":True} and calls==[4]
    assert tested==[{"verified":True},{"rebuilt":True}]
    def failed(proven):raise RuntimeError("native regression failed")
    monkeypatch.setattr(replay_test,"run_local_regressions",failed)
    with pytest.raises(RuntimeError,match="native regression failed"):
        replay_test.ensure_build(4)


def test_seek_costs_keep_sync_inside_tail_and_preserve_full_latency():
    import replay_test
    raw=("host seek ready run_id=r target=214 elapsed_us=651000 owned_bytes=100 paused=true\n"
         "host seek application cost run_id=r target=214 prefix_us=1000 engine_us=85000 tail_us=387000 "
         "frame_sync_us=88000 sync_subset_of_tail=true includes_observers=true\n")
    cost=replay_test.seek_application_costs(raw,"r")["requests"][0]
    assert cost["request_to_ready_us"]==651000
    assert cost["outside_advance_calls_us"]==178000
    for invalid in (raw.replace("frame_sync_us=88000","frame_sync_us=388000"),
                    raw.replace("elapsed_us=651000","elapsed_us=100000"),
                    raw.replace("cost run_id=r","cost run_id=other"),
                    raw+raw.splitlines()[1]+"\n"):
        with pytest.raises(RuntimeError):
            replay_test.seek_application_costs(invalid,"r")


@pytest.mark.parametrize("point", ["before", "after"])
@pytest.mark.parametrize("host_seek", [False, True])
def test_historical_cancellation_requires_independent_B_and_complete_suffix(tmp_path, monkeypatch, point, host_seek):
    import json
    import replay_test
    from deterministic_qualification import replay_fidelity
    monkeypatch.setattr(replay_test, "validate_intro_skips", lambda *args: None)
    monkeypatch.setattr(replay_fidelity, "validate_payload_handoff", lambda *args: "recording")
    monkeypatch.setattr(replay_test, "validate_world_resume_timing", lambda *args:
                        {"native_ticks": 120, "elapsed_us": 2_000_000, "viewport_frames": 120})
    # This fixture exercises cancellation/gameplay accounting. Presentation
    # admission is tested separately and cannot be inferred from these rows.
    monkeypatch.setattr(replay_test, "compare_native_poses", lambda *args: {"result": "pass"})
    monkeypatch.setattr(replay_test, "compare_native_presentation", lambda *args: {"result": "pass"})
    fields = "round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs"
    common = " ".join(f"{key}=0" for key in fields.split())
    def row(label, tick):
        return f"[ReplayQualification] {label} ordinal={tick} frame={tick} round=0 health=100"
    def raw(run_id, cancelled):
        lines = [f"boundaries started run_id={run_id} native_frame=204"]
        owner = "runtime" if cancelled else "control"
        lines += [f"wind construction started owner={owner} policy=zero_initial_distance_v1 tick=0",
                  f"wind construction stopped owner={owner} policy=zero_initial_distance_v1 initialized=3 failed=false detached=true",
                  f"replay rendering started owner={owner} policy=fixed60_render_v7 tick=0",
                  f"replay rendering stopped owner={owner} policy=fixed60_render_v7 constructed=360 completed=360 failed=false detached=true"]
        for tick in range(205, 331):
            lines += [f"[ReplayQualification] boundary ordinal={tick - 204} phase=actor_tail sample_version=2 frame={tick} {common}", row("trajectory", tick)]
            if cancelled and tick == 205:
                lines += [row("historical_target", 205), f"historical A captured run_id={run_id} tick=205 combat_particles=7"]
                if host_seek and point=="before":
                    lines += ["callback checkpoint tick=205 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2"]
            if cancelled and tick == 210:
                lines += [row("historical_original", 210)]
                if host_seek:
                    lines += [f"host seek accepted run_id={run_id} checkpoint=205 origin=210 target=208 aliases_rejected=true",
                              f"host seek phase run_id={run_id} phase=2 pending=true commit_decided=false"]
                    if point=="after":
                        lines += ["restore request prepared target=205 original=210 owned_bytes=2048 complete_B=true",
                                  f"host seek phase run_id={run_id} phase=3 pending=true commit_decided=false",
                                  f"host seek phase run_id={run_id} phase=4 pending=true commit_decided=false",
                                  f"historical topology admitted run_id={run_id} target_tick=205 original_tick=210 particle_birth=true"]
                if point == "after":
                    lines += [row("historical_restored", 205),
                              f"host seek publication observed run_id={run_id} tick=205" if host_seek else f"historical target held run_id={run_id} tick=205 elapsed_us=500000"]
                lines += [f"historical cancellation injected run_id={run_id} point={point} tick={210 if point == 'before' else 205}"+
                          (" preparation_pending=true render_command_pending=true gpu_completion_pending=false" if host_seek and point=="before" else "")]
                if host_seek:
                    lines += [f"host seek phase run_id={run_id} phase=9 pending=true commit_decided=false"]
                    if point=="before":lines += [
                        "restore preparation failed participant=preparation_cancelled code=31 target=205 original=210 before_publication=true",
                        "restore preparation recovery completed original=210 observed=210 ground_released=true render_released=true before_A_publication=true"]
                    lines += [f"host seek phase run_id={run_id} phase=10 pending=false commit_decided=false"]
                lines += [row("historical_recovered", 210), f"historical cancellation recovered run_id={run_id} point={point} tick=210"]
                if host_seek:
                    lines += [f"host seek released run_id={run_id} tick=210 surface_pending=false",
                              "seek checkpoint retired tick=210 application_idle=true"]
                lines += [f"historical combat resumed run_id={run_id} held_tick=210"]
        lines += [f"native display boundary run_id={run_id} tick={tick} generation=0 gpu_readbacks=false" for tick in range(211,331)]
        if cancelled:
            lines += [f"historical capture cost run_id={run_id} elapsed_us=100 gpu_bytes=1024 diagnostic_gpu_readbacks=false transaction_readback_maps=0 readback_scope=observer_and_transaction"]
            if point == "after":
                lines += [f"historical restore cost run_id={run_id} elapsed_us=200 owned_bytes=2048 diagnostic_gpu_readbacks=false transaction_readback_maps=0 readback_scope=observer_and_transaction"]
        lines += [f"boundaries completed run_id={run_id} observations=126 detached=true"]
        if cancelled and host_seek and point=="before":
            lines += ["callback admission stopped latent=0 streamable=0 detached=true"]
        return "\n".join(lines)
    def report(run_id, mode, text):
        raw_path = tmp_path / (run_id + ".log")
        raw_path.write_text(text)
        proof = {"sha256": "same", "pid": 1 if mode == "stock" else 2, "process_created": run_id}
        document = {"run_id": run_id, "mode": mode, "result": "captured", "skip_intros": True, "native_fixed_seed": True, "serial_particles": True,
                    "historical_cancel": point if mode == "runtime" else "",
                    "identities": dict.fromkeys(("runtime", "observer", "framework", "ucrt", "physx", "game", "replay"), "same"),
                    "cleanup": {"complete": True, "games_remaining": 0},
                    "loaded_runtime": dict(proof, verification="owned_process_runtime_absent" if mode == "stock" else "owned_process_mapped_file_and_sha256"),
                    "raw_log": {"path": str(raw_path), "sha256": sha256_file(raw_path)}}
        document.update({"loaded_" + key: proof for key in ("observer", "framework", "ucrt", "physx")})
        if host_seek and mode=="runtime":document.update(host_seek=True,historical_advanced_tick=210)
        if host_seek and mode=="runtime" and point=="before":document['callback_admission_policy']='quiescent_streamable_v2'
        path = tmp_path / (run_id + ".json")
        path.write_text(json.dumps(document))
        return path
    native = report("native", "stock", raw("native", False))
    text = raw("executor", True)
    result = replay_test.compare_historical_combat(native, report("executor", "runtime", text))
    assert result["result"] == "pass" and result["continuation_ticks"] == 120 and not result["milestone_complete"]
    assert result["visual_coherence"]["result"] == "incomplete"
    assert result["pixel_diagnostics"]["result"] == "not_run"
    assert result["costs"]["restore_plus_resimulation"]["result"] == "not_measured"
    assert result["seek_performance"]["result"] == "not_measured"
    if host_seek:
        assert result["seek_request"]["recovered_tick"]==210
        for bad in (text.replace("phase=9 pending=true", "phase=5 pending=true"),
                    text.replace("seek checkpoint retired", "missing retirement")):
            with pytest.raises(RuntimeError):
                replay_test.compare_historical_combat(native,report("executor","runtime",bad))
    with pytest.raises(RuntimeError, match="native display boundary"):
        replay_test.compare_historical_combat(native, report("executor", "runtime", text.replace("tick=330 generation=0", "tick=329 generation=0")))
    report("executor", "runtime", text)
    bad = text.replace(row("historical_recovered", 210), row("historical_recovered", 210).replace("health=100", "health=99"))
    assert replay_test.compare_historical_combat(native, report("executor", "runtime", bad))["first_mismatch"] == "recovered_B"
    for bad in (text.replace("historical cancellation injected", "missing injection"),
                text.replace(row("trajectory", 330), "missing final tick")):
        with pytest.raises(RuntimeError):
            replay_test.compare_historical_combat(native, report("executor", "runtime", bad))


def test_native_pose_gate_rejects_missing_duplicate_and_changed_evaluations():
    from replay_test import compare_native_poses
    def stream(run, resumed):
        lines = []
        for tick in list(range(185, 205)) + list(range(206, 329)):
            generation = int(resumed and tick >= 206)
            key = f"run_id={run} tick={tick} generation={generation}"
            lines += [f"native pose publication {key} ordinal={i} player=0 bones=4 mapping=abc hash=def" for i in range(2)]
            lines += [f"native pose completed {key} evaluations=2"]
        return "\n".join(lines)
    native, restored = stream("n", False), stream("r", True)
    reports = [{"run_id": "n"}, {"run_id": "r"}]
    assert compare_native_poses([native, restored], reports, 205, "")["result"] == "pass"
    checkpoint_reports=[reports[0],dict(reports[1],host_seek_repeat=True,host_seek_first_target=205,host_seek_target=208)]
    assert compare_native_poses([native,restored],checkpoint_reports,208,"")["result"]=="pass"
    with pytest.raises(RuntimeError,match="missing native pose continuation"):
        compare_native_poses([native,restored.replace("generation=1","generation=2")],checkpoint_reports,208,"")
    paired=restored.replace("generation=1","generation=2")+"\n"+"\n".join(line for line in restored.splitlines() if any(f"tick={tick} generation=1 " in line for tick in range(207,211)))
    pair_reports=[reports[0],dict(reports[1],host_seek_repeat=True,checkpoint_pair=True)]
    assert compare_native_poses([native,paired],pair_reports,208,"")["result"]=="pass"
    bad_pair=paired.replace("tick=210 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=def","tick=210 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=bad")
    assert compare_native_poses([native,bad_pair],pair_reports,208,"")["first_seek_first_mismatch"]==210
    later=restored.replace("generation=1","generation=2")+"\n"+"\n".join(line for line in restored.splitlines() if any(f"tick={tick} generation=1 " in line for tick in range(206,221)))
    later_reports=[reports[0],dict(reports[1],host_seek_repeat=True,host_seek_target=208,host_seek_first_target=220)]
    assert compare_native_poses([native,later],later_reports,208,"")["result"]=="pass"
    missing="\n".join(line for line in later.splitlines() if "tick=217 generation=1 " not in line)
    with pytest.raises(RuntimeError,match="missing native pose first_seek at tick 217"):
        compare_native_poses([native,missing],later_reports,208,"")
    changed = restored.replace("tick=306 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=def",
                               "tick=306 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=bad")
    assert compare_native_poses([native, changed], reports, 205, "")["continuation_first_mismatch"] == 306
    for broken in (restored.replace("evaluations=2", "evaluations=3", 1),
                   restored + "\n" + restored.splitlines()[0],
                   restored.replace("native pose completed", "missing completion", 1)):
        with pytest.raises(RuntimeError):
            compare_native_poses([native, broken], reports, 205, "")


@pytest.mark.parametrize("advanced_tick", [209, 210])
def test_historical_boundary_rewind_requires_exact_transaction_witness(advanced_tick):
    from tools.deterministic_qualification.replay_fidelity import validate_boundary_capture
    fields = "round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs"
    common = " ".join(f"{key}=0" for key in fields.split())
    marker = f"historical combat committed run_id=r from_tick={advanced_tick} to_tick=205"
    def log(frames, witness=marker):
        lines = ["boundaries started run_id=r native_frame=205"]
        for i, frame in enumerate(frames, 1):
            if i == 3:
                lines.append(witness)
            lines.append(f"[ReplayQualification] boundary ordinal={i} phase=actor_tail sample_version=2 frame={frame} {common}")
        lines.append(f"boundaries completed run_id=r observations={len(frames)} detached=true")
        return "\n".join(lines)
    valid = log([205, advanced_tick, 206, 207])
    assert len(validate_boundary_capture(valid, "r", historical_restore=True, historical_advanced_tick=advanced_tick)) == 4
    for invalid in [log([205, advanced_tick, 206], ""), log([205, advanced_tick, 204]), log([205, advanced_tick, 206, advanced_tick, 206]), ""]:
        with pytest.raises(RuntimeError):
            validate_boundary_capture(invalid, "r", historical_restore=True, historical_advanced_tick=advanced_tick)
    with pytest.raises(RuntimeError):
        validate_boundary_capture(valid, "r")
    admitted=valid.replace(marker,marker.replace("committed","execution admitted")+" B_retained=true")
    assert len(validate_boundary_capture(admitted,"r",historical_restore=True,
        historical_advanced_tick=advanced_tick,retained_execution=True))==4
    for invalid in (valid,admitted.replace("B_retained=true","B_retained=false"),
                    admitted.replace("execution admitted","committed")):
        with pytest.raises(RuntimeError):
            validate_boundary_capture(invalid,"r",historical_restore=True,
                historical_advanced_tick=advanced_tick,retained_execution=True)

    zero_marker="historical combat execution admitted run_id=r from_tick=205 to_tick=205 B_retained=true"
    admitted_marker=marker.replace("committed","execution admitted")+" B_retained=true"
    zero=admitted.replace(admitted_marker,admitted_marker+"\n"+zero_marker)
    zero_plan=[(advanced_tick,205),(205,205)]
    assert len(validate_boundary_capture(zero,"r",historical_restore=True,retained_execution=True,expected_rewinds=zero_plan))==4
    for invalid in (zero.replace(zero_marker,""),zero.replace(zero_marker,zero_marker.replace("from_tick=205","from_tick=206")),
                    admitted+"\n"+zero_marker):
        with pytest.raises(RuntimeError):
            validate_boundary_capture(invalid,"r",historical_restore=True,retained_execution=True,expected_rewinds=zero_plan)

    # Two traversals must be authorized by the caller, with each commit between
    # its actual original boundary and first restored callback. Duplicate or
    # early commit markers cannot manufacture a permitted rewind.
    lines=["boundaries started run_id=r native_frame=205"]
    for i,frame in enumerate([205,210,206,209,206,207],1):
        if i in (3,5):
            lines.append(f"historical combat committed run_id=r from_tick={210 if i==3 else 209} to_tick=205")
        lines.append(f"[ReplayQualification] boundary ordinal={i} phase=actor_tail sample_version=2 frame={frame} {common}")
    lines.append("boundaries completed run_id=r observations=6 detached=true")
    repeated="\n".join(lines)
    plan=[(210,205),(209,205)]
    assert len(validate_boundary_capture(repeated,"r",historical_restore=True,expected_rewinds=plan))==6
    second="historical combat committed run_id=r from_tick=209 to_tick=205"
    for invalid in [repeated.replace(second,""), second+"\n"+repeated.replace(second,""), repeated+"\n"+second]:
        with pytest.raises(RuntimeError):
            validate_boundary_capture(invalid,"r",historical_restore=True,expected_rewinds=plan)
    with pytest.raises(RuntimeError):
        validate_boundary_capture(repeated,"r",historical_restore=True)


def test_backward_B_recovery_requires_requested_plan_and_completed_ownership():
    from tools.deterministic_qualification.replay_fidelity import validate_boundary_capture, expected_recovery_rewind
    fields="round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs"
    common=" ".join(f"{key}=0" for key in fields.split())
    owner="seek ownership state=Recovered B_tick=2504 target=5750 current_tick=2504 completed_tail_tick=5750 B_retained=false commit_decided=false executed_ticks=5580 executed_intervals=5580"
    receipt="historical cancellation recovered run_id=r point=after tick=2504"
    lines=["boundaries started run_id=r native_frame=170"]
    for i,frame in enumerate([170,2504,171,5750,2505,2624],1):
        if i==3:lines.append("historical combat execution admitted run_id=r from_tick=2504 to_tick=170 B_retained=true")
        if i==5:lines.extend([owner,receipt])
        lines.append(f"[ReplayQualification] boundary ordinal={i} phase=actor_tail sample_version=2 frame={frame} {common}")
    lines.append("boundaries completed run_id=r observations=6 detached=true")
    text="\n".join(lines)
    args=dict(historical_restore=True,expected_rewinds=[(2504,170)],retained_execution=True)
    assert len(validate_boundary_capture(text,"r",**args,recovery_rewind=(5750,2504)))==6
    for invalid in (text.replace(owner,""),text.replace(receipt,""),text.replace("commit_decided=false","commit_decided=true"),
                    text.replace("current_tick=2504","current_tick=2505"),text+"\n"+receipt,
                    text.replace("frame=2505","frame=2503")):
        with pytest.raises(RuntimeError):validate_boundary_capture(invalid,"r",**args,recovery_rewind=(5750,2504))
    with pytest.raises(RuntimeError):validate_boundary_capture(text,"r",**args)
    with pytest.raises(RuntimeError):validate_boundary_capture(text,"r",**args,recovery_rewind=(5750,220))
    report=dict(historical_advanced_tick=2504,host_seek_target=5750,seek_settlement_failure=True,seek_advance_failure=True,historical_cancel="after")
    assert expected_recovery_rewind(report)==(5750,2504)
    assert expected_recovery_rewind({**report,"host_seek_target":214}) is None
    assert expected_recovery_rewind({**report,"historical_cancel":""}) is None


def test_particle_copy_requires_combat_writes_and_retirement():
    from replay_test import validate_particle_copy
    lines = []
    for tick in (205, 210):
        lines += [f"boundary ordinal={tick} phase=round_sequence sample_version=2 frame={tick} cursor=10 "
                  "input=1 source_active=true manager_phase=2 move_state=0 round_state=2 round=1 world_mode=2",
                  f"application pause started run_id=r native_frame={tick} epoch=30 applications=10 task_retired=true"]
    lines += ["cpu replacement published run_id=r tick=205 original=11 replacement=12 component=13",
              "cpu replacement undone run_id=r tick=205 elapsed_us=250000 updates=30 surface_frames=10 bytes=4096 original_restored=true",
              "particle copy captured run_id=r tick=205 system=1 pool=2 resources=6 gpu_complete=true reserved_payload_and_metadata_bytes=176161000",
              "historical cpu replacement prepared run_id=r captured_tick=205 held_tick=210 original_retired=true address_reused=false independent_storage=true historical_publication=false bytes=1024",
              "particle textures installed run_id=r tick=210 captured_tick=205 images_match=true gpu_complete=true release_blocked=true resume_blocked=true cpu_state=B target_presented=false",
              "particle textures recovered run_id=r tick=210 images_match=true gpu_complete=true cancelled_after_publication=true uncommitted=false selection=1",
              "particle copy retirement run_id=r tick=210 cancel_release_blocked=true cancel_retired=true timeout_release_blocked=true timeout_retired=true",
              "particle copy survived run_id=r captured_tick=205 held_tick=210 system=1 pool=2 retained_matches=true changed_sources=2 reserved_payload_and_metadata_bytes=234881256 gpu_complete=true",
              "particle object leases retired run_id=r tick=210 registered=0",
              "particle copy released run_id=r tick=210 in_flight=false",
              "particle copy native playback resumed run_id=r held_tick=210"]
    raw = "\n".join(lines)
    for tick in (205, 210):
        raw += (f"\nshared particle owner run_id=r tick={tick} component=20 emitters=1 tiles=13 object=ParticleComponent fixture"
                f"\nshared particle partition run_id=r tick={tick} owners=1 allocated=13 free=65523 complete=true disjoint=true")
    raw = ("particle object lease run_id=r tick=205 objects=3 bytes=256 acquired=true code=0\n"
           "particle object lease run_id=r tick=210 objects=4 bytes=288 acquired=true code=0\n") + raw
    assert validate_particle_copy(raw, "r")["changed_sources"] == 2
    for invalid in (raw.replace("changed_sources=2", "changed_sources=0"),
                    raw.replace("uncommitted=false", "uncommitted=true"),
                    raw.replace("target_presented=false", "target_presented=true"),
                    raw.replace("cancelled_after_publication=true", "cancelled_after_publication=false"),
                    raw.replace("selection=1", "selection=2"),
                    raw.replace("registered=0", "registered=1"),
                    raw.replace("acquired=true", "acquired=false"),
                    raw.replace("disjoint=true", "disjoint=false"),
                    raw.replace("free=65523", "free=65522"),
                    raw.replace("tiles=13", "tiles=12"),
                    raw.replace("elapsed_us=250000", "elapsed_us=1"),
                    raw.replace("original_restored=true", "original_restored=false"),
                    raw.replace("historical_publication=false", "historical_publication=true"),
                    raw.replace("retained_matches=true", "retained_matches=false"),
                    raw.replace("in_flight=false", "in_flight=true"),
                    raw.replace("timeout_retired=true", "timeout_retired=false"),
                    raw.replace("cancel_release_blocked=true", "cancel_release_blocked=false"),
                    raw.replace("source_active=true", "source_active=false"),
                    raw.replace("held_tick=210", "held_tick=211")):
        with pytest.raises(RuntimeError):
            validate_particle_copy(invalid, "r")


@pytest.mark.workflow
@pytest.mark.parametrize("stage", ["restore", "restore-probe"])
def test_retired_intro_restore_cannot_build_or_launch(stage, monkeypatch, capsys):
    from tools import replay_test
    monkeypatch.setattr(replay_test.sys, "argv", ["replay_test.py", stage])
    def unexpected_work(*args, **kwargs):
        pytest.fail("retired intro experiment reached build or deployment")
    monkeypatch.setattr(replay_test, "build", unexpected_work)
    monkeypatch.setattr(replay_test, "capture", unexpected_work)
    with pytest.raises(SystemExit) as error:
        replay_test.main()
    assert error.value.code == 2
    assert "pauses during character intros and is retired" in capsys.readouterr().err


def test_journal_replace_keeps_recovery_barrier_during_windows_lock(tmp_path, monkeypatch):
    from tools.deterministic_qualification import run_journal
    target = tmp_path / "journal.json"
    target.write_text('{"state":"old"}')
    replace = run_journal.os.replace
    attempts = []
    def locked(source, destination):
        attempts.append(1)
        assert target.read_text() == '{"state":"old"}'
        if len(attempts) < 3:
            error = PermissionError("temporary reader lock")
            error.winerror = 5
            raise error
        replace(source, destination)
    monkeypatch.setattr(run_journal.os, "replace", locked)
    monkeypatch.setattr(run_journal.time, "sleep", lambda _: None)
    run_journal.atomic_json(target, {"state": "new"})
    assert len(attempts) == 3 and '"new"' in target.read_text()
    assert not target.with_suffix(".json.tmp").exists()

    def denied(*args):
        error = PermissionError("persistent lock")
        error.winerror = 5
        raise error
    monkeypatch.setattr(run_journal.os, "replace", denied)
    with pytest.raises(PermissionError):
        run_journal.atomic_json(target, {"state": "later"})
    assert '"new"' in target.read_text()
    assert '"later"' in target.with_suffix(".json.tmp").read_text()


def test_restore_evidence_rejects_missing_duplicate_and_out_of_order_trial(tmp_path, monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    from replay_test import validate_small_restore
    rows = [
        "small restore started run_id=r from_tick=360 to_tick=361\n"
        "small restore world captured run_id=r timers=6 owned_bytes=1296 independent_callbacks=true capacity_rejected=true retained=true\n"
        "small restore world rebuilt run_id=r old_storage=1000 new_storage=2000 restore_capacity_rejected=true\n"
        "small restore native CRT verified run_id=r draws=2 native_equal=true restored=true",
        "restore_trial ordinal=1 phase=callback_a30 sample_version=2 frame=361 rest",
        "restore_trial ordinal=2 phase=round_sequence sample_version=2 frame=361 rest",
        "small restore completed run_id=r from_tick=360 to_tick=361 trial_ticks=1 "
        "trial_boundaries=2 native_equal=true hash_equal=true components_equal=true "
        + "canonical_hash=" + "a" * 64 + " elapsed_us=3722",
    ]
    rows[0] += "\n" + "\n".join(
        f"small restore metadata rejected run_id=r field={field} code=8 unchanged=true pending_event=true"
        for field in ("coordinate", "ucrt", "task"))
    rows[2] += "\n" + "\n".join(
        f"small restore cancelled run_id=r phase={phase} original_tick=361 target_tick=360 "
        f"queries={queries} target_observed={observed} original_restored=true nested_rejected=true pending_event=true"
        for phase, queries, observed in (("before_writes", 1, "false"), ("before_commit", 2, "true")))
    raw = "\n".join(rows)
    assert validate_small_restore(raw, "r")["trial_ticks"] == 1
    for invalid in ("\n".join(rows[:2] + rows[3:]), raw + "\n" + rows[1],
                    "\n".join([rows[1], rows[0], rows[2], rows[3]]),
                    raw.replace("native_equal=true", "native_equal=false"),
                    raw.replace("components_equal=true", "components_equal=false"),
                    raw.replace("independent_callbacks=true", "independent_callbacks=false"),
                    raw.replace("retained=true", "retained=false"),
                    raw.replace("timers=6", "timers=0"),
                    raw.replace("new_storage=2000", "new_storage=1000"),
                    raw.replace("new_storage=2000", "new_storage=01000"),
                    raw.replace("new_storage=2000", "new_storage=0"),
                    raw.replace("restore_capacity_rejected=true", "restore_capacity_rejected=false"),
                    raw.replace("original_restored=true", "original_restored=false"),
                    raw.replace("target_observed=true", "target_observed=false"),
                    raw.replace("phase=before_commit", "phase=before_writes"),
                    raw.replace("queries=2", "queries=1"),
                    raw.replace("field=ucrt", "field=other"), raw.replace("code=8", "code=0"),
                    raw + "\nsmall restore failed phase=undo", raw.replace("run_id=r", "run_id=stale")):
        with pytest.raises(RuntimeError, match="small restore"):
            validate_small_restore(invalid, "r")


@pytest.mark.parametrize("changed", ["runtime", "observer", "framework", "game", "replay"])
def test_native_gate_rejects_stale_producer_before_launch(tmp_path, monkeypatch, changed):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    import replay_test

    paths = {}
    for key in ("runtime", "observer", "framework", "game", "replay"):
        paths[key] = tmp_path / key
        paths[key].write_bytes(key.encode())
    report = {"result": "pass", "completion": {"full_match": True},
        "setup_observations_compared": 840,
        "identities": {key: sha256_file(path) for key, path in paths.items()}}
    monkeypatch.setattr(replay_test, "compare_passive_controls", lambda *a, **kw: report)
    args = SimpleNamespace(dll=paths["runtime"], replay_mod=paths["observer"],
        framework=paths["framework"], game_executable=paths["game"], replay=paths["replay"],
        include_setup=True)
    replay_test.require_native_baseline(args)
    paths[changed].write_bytes(b"changed producer")
    with pytest.raises(RuntimeError, match="exact binaries"):
        replay_test.require_native_baseline(args)
    report["native_runtime_absence_verified"]=True
    if changed=="runtime": replay_test.require_native_baseline(args)
    else:
        with pytest.raises(RuntimeError,match="exact binaries"): replay_test.require_native_baseline(args)


def test_interior_pause_requires_ordered_independent_witnesses(monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    from replay_test import validate_interior_pause
    lines = ["interior pause started run_id=probe native_frame=360 epoch=900 pending_event=true",
        "interior pause held run_id=probe native_frame=360 epoch=900 elapsed_us=1000000 "
        "application_updates=300 surface_frames=59 surface_bytes=8294400 unchanged=true pending_event=true",
        "interior pause resumed run_id=probe held_tick=360 native_callbacks=4"]
    raw = "\n".join(lines)
    assert validate_interior_pause(raw, "probe")["surface_frames"] == 59
    assert not validate_interior_pause(raw, "probe")["active_combat_verified"]
    combat_hold = raw.replace("=360", "=1000")
    boundary = ("boundary ordinal=3994 phase=round_sequence sample_version=2 frame=1000 round=0 cursor=156 "
        "input_round=0 source_active=true manager_phase=2 move_state=0 round_state=2 world_mode=2\n")
    assert validate_interior_pause(boundary + combat_hold, "probe")["active_combat_verified"]
    assert validate_interior_pause((boundary + combat_hold).replace("frame=1000 ", "frame=211 ").replace("held_tick=1000 ", "held_tick=211 "), "probe")["tick"] == 211
    for invalid_combat in (combat_hold, combat_hold + "\n" + boundary,
            boundary.replace("source_active=true", "source_active=false") + combat_hold,
            boundary.replace("round_state=2", "round_state=7") + combat_hold,
            boundary.replace("cursor=156", "cursor=0") + combat_hold):
        with pytest.raises(RuntimeError, match="combat interior pause lacks"):
            validate_interior_pause(invalid_combat, "probe")
    for invalid in (raw.replace("epoch=900", "epoch=901", 1), raw + "\n" + lines[1],
            raw.replace("pending_event=true", "pending_event=false", 1),
            raw.replace("surface_frames=59", "surface_frames=0"),
            raw.replace("elapsed_us=1000000", "elapsed_us=999999"),
            raw.replace("native_callbacks=4", "native_callbacks=0"), "\n".join(reversed(lines))):
        with pytest.raises(RuntimeError, match="interior pause lacks"):
            validate_interior_pause(invalid, "probe")
    with pytest.raises(RuntimeError, match="interior pause lacks"):
        validate_interior_pause(raw, "another-run")


def test_interior_control_requires_real_activation_and_owned_input(monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    from replay_test import validate_interior_control
    rows = ["interior pause started run_id=r native_frame=360 epoch=900 pending_event=true",
            "replay resume control hwnd=123 screen_x=100 screen_y=80",
            "replay resume activated request=1 surface_frame=15",
            "interior control verified run_id=r native_frame=360 requests=1 application_updates=300 pending_event=true unchanged=true",
            "interior pause held run_id=r native_frame=360 epoch=900 elapsed_us=1000000 application_updates=300 surface_frames=59 surface_bytes=3686400 unchanged=true pending_event=true",
            "interior pause resumed run_id=r held_tick=360 native_callbacks=3"]
    report = {"run_id": "r", "loaded_observer": {"pid": 7}, "ui_input": {
        "method": "Windows SendInput", "pid": 7, "hwnd": 123, "x": 100, "y": 80, "button_released": True}}
    raw = "\n".join(rows)
    assert validate_interior_control(raw, report)["independent_activation_verified"]
    for invalid in (raw.replace(rows[2], ""), raw + "\n" + rows[2],
                    raw.replace("request=1", "request=2"), raw.replace("screen_x=100", "screen_x=101")):
        with pytest.raises(RuntimeError, match="interior control lacks"):
            validate_interior_control(invalid, report)
    with pytest.raises(RuntimeError, match="interior control lacks"):
        validate_interior_control(raw, {**report, "ui_input": {}})


def test_world_pause_and_all_counters_do_not_complete_execution_gate(monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    from replay_test import execution_gate_coverage

    experiment = {"result": "pass", "native_boundaries_compared": 100,
        "completion": {"full_match": True}, "empty_interval_probe": {"verified": True},
        "world_pause": {"elapsed_us": 1_000_000}, "external_interior_pause": True,
        "native_execution_coverage": {"zero_tick_intervals": 1, "multi_tick_intervals": 2,
            "publication_observer_present": True, "repeated_ticks_without_new_publication": 1,
            "move_state_3_ticks": 3},
        "executor": {"yield_every_tick": True, "zero_tick_intervals": 1,
            "multi_tick_intervals": 2, "repeat_requests": 1, "move_state_ticks": 3}}
    coverage = execution_gate_coverage(experiment)
    assert coverage["result"] == "incomplete" and not coverage["complete"]
    assert coverage["missing"] == ["external_interior_pause_and_continuation",
                                   "active_combat_interior_pause_and_continuation",
                                   "interactive_controls_during_interior_pause"]
    experiment["interior_pause"] = {"independent_hold_verified": True,
                                    "surface_frames": 60, "interactive_controls_verified": True}
    assert execution_gate_coverage(experiment)["missing"] == ["active_combat_interior_pause_and_continuation",
                                                             "interactive_controls_during_interior_pause"]
    experiment["interior_pause"]["active_combat_verified"] = True
    assert execution_gate_coverage(experiment)["missing"] == ["interactive_controls_during_interior_pause"]
    experiment["result"] = "fail"
    assert not any(execution_gate_coverage(experiment)["demonstrated"].values())


def test_execution_gate_excludes_stale_and_rechecks_claimed_coverage(tmp_path, monkeypatch):
    import argparse
    import json
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    import replay_test as runner
    monkeypatch.setattr(runner, "OUTPUT", tmp_path)
    monkeypatch.setattr(runner, "require_bootstrap", lambda _: None)
    monkeypatch.setattr(runner, "require_native_baseline", lambda _: None)
    monkeypatch.setattr(runner, "sha256_file", lambda path: str(path))
    (tmp_path / "baseline-a.json").write_text(json.dumps({"identities": {"config": "baseline-config"}}))
    args = argparse.Namespace(dll="runtime", replay_mod="observer", framework="framework",
        game_executable="game", replay="replay")
    stage = tmp_path / "equivalence-stage.json"
    report = tmp_path / "equivalence-executor.json"
    stage.write_text(json.dumps({"result": "pass", "native_control_report": "native.json",
        "execution_gate": {"complete": True}}))
    identities = {key: key for key in ("runtime", "observer", "framework", "game", "replay")}
    identities["runtime"] = "previous-runtime"
    report.write_text(json.dumps({"identities": identities}))
    gate = runner.collect_execution_gate(args)
    assert not gate["complete"] and gate["evidence"] == []
    assert gate["excluded"][0]["reason"] == "failed or stale identities"
    identities["runtime"] = "runtime"
    report.write_text(json.dumps({"identities": identities}))
    monkeypatch.setattr(runner, "compare_passive_controls", lambda *args: {"result": "fail"})
    with pytest.raises(RuntimeError, match="no longer matches native"):
        runner.collect_execution_gate(args)


def test_move_state_probe_requires_distinct_native_traversal():
    from deterministic_qualification.replay_fidelity import validate_move_state_interval
    rows = ["move state interval started run_id=r native_frame=900 requested_state=3"]
    for ordinal, (phase, frame, state) in enumerate((
            ("callback_950", 900, 3), ("callback_a30", 901, 3),
            ("callback_870", 901, 3), ("actor_tail", 901, 0)), 1):
        rows.append(f"[ReplayQualification] boundary ordinal={ordinal} phase={phase} frame={frame} move_state={state}")
    rows.append("move state interval completed run_id=r native_frame=901 source_unchanged=true state=0")
    raw = "\n".join(rows)
    assert validate_move_state_interval(raw, "r", True)["to_tick"] == 901
    for invalid in (raw.replace(rows[2], ""), raw.replace(rows[2], rows[2] + "\n" + rows[2]),
            raw.replace("phase=callback_950", "phase=input_cache_publication"),
            raw.replace("requested_state=3", "requested_state=2")):
        with pytest.raises(RuntimeError):
            validate_move_state_interval(invalid, "r", True)
    with pytest.raises(RuntimeError, match="unrequested"):
        validate_move_state_interval(raw, "r", False)


def test_native_publications_distinguish_identical_samples_from_repeat_ticks():
    from tools.deterministic_qualification.replay_fidelity import summarize_native_execution
    def row(phase):
        return {"phase": phase, "move_state": "0", "inputs": "0,0"}
    rows = [row(phase) for phase in ("input_cache_publication", "callback_a30",
        "input_cache_publication", "callback_a30", "callback_a30", "actor_tail", "actor_tail")]
    result = summarize_native_execution(rows)
    assert result["input_cache_publications"] == 2
    assert result["repeated_ticks_without_new_publication"] == 1
    assert result["multi_tick_intervals"] == result["zero_tick_intervals"] == 1
    historical = summarize_native_execution([r for r in rows if r["phase"] != "input_cache_publication"])
    assert historical["repeated_ticks_without_new_publication"] is None
    move_tick = {**row("callback_a30"), "move_state": "3"}
    move = summarize_native_execution([row("callback_950"), move_tick, row("callback_870"),
        row("input_cache_publication"), move_tick, row("actor_tail")])
    assert move["move_state_3_ticks"] == 1  # The state byte alone does not identify the move path.
    with pytest.raises(RuntimeError, match="no intervening simulation"):
        summarize_native_execution([row("input_cache_publication"), *rows])
    with pytest.raises(RuntimeError, match="inside an interval"):
        summarize_native_execution(rows[:-2])


@pytest.mark.parametrize("yielded,requested,accepted", [(3, "true", True), (2, "true", False), (0, "false", False)])
def test_executor_evidence_requires_every_requested_boundary(tmp_path, monkeypatch, yielded, requested, accepted):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[2]))
    from replay_test import executor_proof

    raw = tmp_path / "executor.log"
    raw.write_text("executor started run_id=replay-test native_frame=100\n"
        f"world executor completed run_id=replay-test worlds=3 groups=21 tasks=30 manager_tasks=3 manager_yields={yielded} idle=true disabled=true\n"
        "engine executor completed run_id=replay-test intervals=3 idle=true disabled=true\n"
        "world arena completed run_id=replay-test scopes=50 max_retained_bytes=65536 probe_checks=10 empty=true\n"
        "executor boundaries run_id=replay-test completed_intervals=3 zero_intervals=0 "
        "multi_intervals=0 repeat_requests=0 move_state_ticks=0 "
        f"yielded_boundaries={yielded} yield_every_tick={requested}\n"
        "executor completed run_id=replay-test ticks=3 intervals=3 publications=3 "
        "native_frame=103 disabled=true\n"
        "application executor stopped intervals=3 idle=true disabled=true\n")
    report = {"run_id": "replay-test", "raw_log": {"path": str(raw), "sha256": sha256_file(raw)}}
    if accepted:
        assert executor_proof(report, True)["yielded_boundaries"] == 3
        original = raw.read_text()
        raw.write_text(original + "[HorseMod] outer-tick accounting failed status=identity_mismatch\n")
        report["raw_log"]["sha256"] = sha256_file(raw)
        with pytest.raises(RuntimeError, match="failed legacy accounting"):
            executor_proof(report, True)
        raw.write_text(original.replace("application executor stopped intervals=3", "application executor stopped intervals=2"))
        report["raw_log"]["sha256"] = sha256_file(raw)
        with pytest.raises(RuntimeError, match="application-tail evidence"):
            executor_proof(report, True)
        raw.write_text(original.replace("worlds=3", "worlds=0"))
        report["raw_log"]["sha256"] = sha256_file(raw)
        with pytest.raises(RuntimeError, match="world-tail evidence"):
            executor_proof(report, True)
        raw.write_text(original.replace("empty=true", "empty=false"))
        report["raw_log"]["sha256"] = sha256_file(raw)
        with pytest.raises(RuntimeError, match="world-arena evidence"):
            executor_proof(report, True)
        raw.write_text(original)
        report["raw_log"]["sha256"] = sha256_file(raw)
        raw.write_text(raw.read_text() + "changed")
        with pytest.raises(RuntimeError, match="log changed"):
            executor_proof(report, True)
    else:
        with pytest.raises(RuntimeError, match="boundary coverage"):
            executor_proof(report, True)


def test_replay_deploys_requested_binary_and_rejects_other_loaded_copy(tmp_path, monkeypatch):
    monkeypatch.setattr(replay_run, "list_game_processes", lambda: ())
    monkeypatch.setattr(process_control, "list_game_processes", lambda: ())
    source = tmp_path / "build.dll"
    deployed = tmp_path / "mod" / "main.dll"
    deployed.parent.mkdir()
    source.write_bytes(b"new runtime")
    deployed.write_bytes(b"old runtime")
    with replay_run.ReplayRun(source, deployed, tmp_path / "report.json") as run:
        assert deployed.read_bytes() == source.read_bytes()
        monkeypatch.setattr(run.resources, "live_processes",
                            lambda **kwargs: (SimpleNamespace(pid=42),))
        mapped = [SimpleNamespace(path=str(source))]
        process = SimpleNamespace(memory_maps=lambda: mapped, create_time=lambda: 123)
        monkeypatch.setattr(replay_run.psutil, "Process", lambda pid: process)
        with pytest.raises(RuntimeError, match="did not load"):
            run.verify_loaded_module(42)
        mapped[:] = [SimpleNamespace(path=str(deployed))]
        assert run.verify_loaded_module(42)["sha256"] == sha256_file(source)
        source.write_bytes(b"changed during capture")
        with pytest.raises(RuntimeError, match="changed during capture"):
            run.verify_loaded_module(42)
        monkeypatch.setattr(run.resources, "live_processes", lambda **kwargs: ())
    assert deployed.read_bytes() == b"old runtime"
    assert run.resources.document["state"] == "clean"


def test_interrupted_replay_deployment_recovers_before_next_capture(tmp_path, monkeypatch):
    monkeypatch.setattr(replay_run, "list_game_processes", lambda: ())
    monkeypatch.setattr(process_control, "list_game_processes", lambda: ())
    source, deployed = tmp_path / "build.dll", tmp_path / "main.dll"
    source.write_bytes(b"new runtime")
    deployed.write_bytes(b"old runtime")
    report = tmp_path / "report.json"
    interrupted = replay_run.ReplayRun(source, deployed, report)
    interrupted.__enter__()  # Simulate process death after durable deployment.
    assert deployed.read_bytes() == b"new runtime"
    journal_before = interrupted.journal_path.read_bytes()
    with pytest.raises(RuntimeError, match="owned by another runner"):
        with replay_run.ReplayRun(source, deployed, tmp_path / "other-report.json"):
            pytest.fail("contender acquired live deployment")
    assert interrupted.journal_path.read_bytes() == journal_before
    assert deployed.read_bytes() == b"new runtime"
    interrupted._release_deployment()  # OS closes handles when the owner dies.
    with replay_run.ReplayRun(source, deployed, tmp_path / "recovered-report.json"):
        assert deployed.read_bytes() == b"new runtime"
    assert deployed.read_bytes() == b"old runtime"


def test_stock_control_proves_absence_and_restores_runtime(tmp_path, monkeypatch):
    monkeypatch.setattr(replay_run, "list_game_processes", lambda: ())
    monkeypatch.setattr(process_control, "list_game_processes", lambda: ())
    source, deployed = tmp_path / "build.dll", tmp_path / "main.dll"
    source.write_bytes(b"new runtime")
    deployed.write_bytes(b"installed runtime")
    with replay_run.ReplayRun(source, deployed, tmp_path / "stock.json",
                              runtime_enabled=False) as run:
        assert not deployed.exists()
        monkeypatch.setattr(run.resources, "live_processes", lambda **kwargs: (SimpleNamespace(pid=42),))
        paths = [SimpleNamespace(path=str(deployed))]
        process = SimpleNamespace(memory_maps=lambda: paths, create_time=lambda: 123)
        monkeypatch.setattr(replay_run.psutil, "Process", lambda pid: process)
        with pytest.raises(RuntimeError, match="stock control loaded"):
            run.verify_loaded_module(42)
        paths.clear()
        assert run.verify_loaded_module(42)["verification"] == "owned_process_runtime_absent"
        monkeypatch.setattr(run.resources, "live_processes", lambda **kwargs: ())
    assert deployed.read_bytes() == b"installed runtime"




def test_same_input_and_saved_pass_do_not_hide_wrong_native_positions(tmp_path):
    import json
    from tools.deterministic_qualification.replay_fidelity import compare_trajectories

    def capture(name, wrong_position=False, seek=False):
        raw = tmp_path / (name + ".log")
        rows = []
        if seek:
            rows.extend((
                "[HorseMod] replay source boundary forced=true source_round=0 source_cursor=120 native_frame=500",
                "[HorseMod] owned replay seek restored target=300 source_end=500 resume_validation=true",
                "[HorseMod] owned replay seek resume verified target=300 source_end=500 verified_frames=200 final=500"))
        for cursor in range(120, 601, 60):
            position = "bad" if wrong_position and cursor == 300 else "same"
            rows.append(f"[HorseMod] replay source boundary forced=false historical=false source_round=0 "
                        f"source_cursor={cursor} positions_valid=true producer_inputs=1/2 resolved_hit_calls=1 "
                        f"p0_sim={position} p0_step=same p0_render=same p1_sim=same p1_step=same p1_render=same")
        raw.write_text("\n".join(rows))
        artifacts = {key: {"sha256": key} for key in (
            "horsemod_dll", "replay_qualification_mod", "replay", "generated_schema", "game_executable")}
        artifacts.update(loaded_horsemod={"sha256": "horsemod_dll",
            "verification": "owned_process_mapped_file_and_sha256"},
            raw_logs={"game": {"path": str(raw), "sha256": sha256_file(raw)}})
        report = tmp_path / (name + ".json")
        report.write_text(json.dumps({"result": "pass", "artifacts": artifacts,
            "cleanup": {"deployment_restored": True, "game_processes_remaining": 0}}))
        return report

    reference = capture("reference")
    candidate = capture("seek", wrong_position=True, seek=True)
    result = compare_trajectories(reference, candidate)
    assert result["result"] == "fail"
    assert result["first_source_boundary"] == [0, 300]
    assert result["different_fields"] == ["p0_sim"]
    assert compare_trajectories(reference, capture("fixed", seek=True))["result"] == "pass"
    candidate_log = tmp_path / "seek.log"
    candidate_log.write_text("\n".join(line for line in candidate_log.read_text().splitlines()
        if "owned replay seek" not in line))
    document = json.loads(candidate.read_text())
    document["artifacts"]["raw_logs"]["game"]["sha256"] = sha256_file(candidate_log)
    candidate.write_text(json.dumps(document))
    with pytest.raises(RuntimeError, match="no completed seek"):
        compare_trajectories(reference, candidate)


@pytest.mark.parametrize('sample_version', (2, 4, 5))
def test_passive_control_rejects_late_start_wrong_clock_and_wrong_request(tmp_path, sample_version):
    import json
    from tools.deterministic_qualification.replay_fidelity import compare_passive_controls

    def capture(mode, start=0, wrong_clock=False, wrong_request=False, name=None, wrong_vital=False,
                boundaries=False, wrong_boundary_order=False, wrong_crt=False, omit_crt=False,
                wrong_ground=False, omit_ground=False):
        name = name or mode
        log = tmp_path / (name + ".log")
        run_id = "run-" + name
        rows = [f"accepted passive trajectory run_id={run_id if not wrong_request else 'stale'} samples=120"]
        rows.append(f"[ReplayQualification] payload handoff run_id={run_id} "
                    f"battle_sha256={'a' * 64} expected_recording_sha256={'b' * 64} "
                    f"actual_recording_sha256={'b' * 64} source_equal=true round=0 cursor=0")
        rows.append(f"[ReplayQualification] initial round snapshot run_id={run_id} version=1 round=0 cursor=0 "
                    "available=true consumed_equal=true raw_equal=false expected_motion_count=0 "
                    "actual_motion_count=0 consumed_difference=4294967295")
        for ordinal in range(1, 121):
            cursor = start + ordinal - 1
            frame = cursor + (1 if wrong_clock and ordinal == 50 else 0)
            vital = "1" if wrong_vital and ordinal == 50 else "0"
            rows.append(f"[ReplayQualification] trajectory ordinal={ordinal} phase=engine_post "
                f"sample_version={sample_version} p0_vital={vital} p1_vital=0 p0_moves=0 p1_moves=0 "
                f"frame={frame} round=0 cursor={cursor} input_round=0 input_time={cursor} "
                f"round_frame={cursor} inputs=0,0 p0_sim=0 p0_step=0 p0_render=0 "
                "p1_sim=0 p1_step=0 p1_render=0")
            if sample_version in (4, 5):
                rows[-1] += ' mt_cursor=0 mt_hash=0000000000000000'
                if not omit_crt:
                    rows[-1] += ' crt_state=' + ('00000001' if wrong_crt and ordinal==50 else '00000000')
            if sample_version == 5 and not omit_ground:
                rows[-1] += ' ground_roots=1 ground_next_id=8 ground_lifecycle=' + ('0000000000000001' if wrong_ground and ordinal==50 else '0000000000000000')
        if boundaries:
            rows.append(f"[ReplayQualification] boundaries started run_id={run_id} native_frame=0")
            observations = [row for row in rows if "trajectory ordinal=" in row]
            for ordinal, observation in enumerate(observations):
                phases = ["callback_a30", "round_sequence"]
                if wrong_boundary_order and ordinal == 49:
                    phases.reverse()
                phases.append("actor_tail")
                for index, phase in enumerate(phases):
                    rows.append(observation.replace("trajectory ordinal=" + str(ordinal + 1),
                        "boundary ordinal=" + str(ordinal * 3 + index + 1))
                        .replace("phase=engine_post", "phase=" + phase)
                        + " source_active=true manager_phase=1 move_state=0 round_state=2 published_count=2 published_pairs=0,0,0,0")
            rows.append(f"[ReplayQualification] boundaries completed run_id={run_id} observations=360 detached=true")
        log.write_text("\n".join(rows))
        identities = {name: name for name in ("observer", "runtime", "replay", "game", "config", "framework")}
        proof = {"pid": 1, "process_created": 1, "sha256": "runtime",
                 "verification": "owned_process_runtime_absent" if mode == "stock"
                                 else "owned_process_mapped_file_and_sha256"}
        report = tmp_path / (name + ".json")
        report.write_text(json.dumps({"result": "captured", "mode": mode, "run_id": run_id,
            "identities": identities, "loaded_runtime": proof,
            "loaded_observer": {"pid": 1, "process_created": 1, "sha256": "observer"},
            "loaded_framework": {"pid": 1, "process_created": 1, "sha256": "framework", "verification": "owned_process_mapped_file_and_sha256"},
            "cleanup": {"complete": True, "games_remaining": 0}, "observations_requested": 120,
            "raw_log": {"path": str(log), "sha256": sha256_file(log)}}))
        return report

    reference, candidate = capture("stock"), capture("runtime")
    assert compare_passive_controls(reference, candidate)["result"] == "pass"
    if sample_version in (4, 5):
        different=compare_passive_controls(reference,capture('runtime',wrong_crt=True,name='wrong-crt'))
        assert different['first_observation']==50 and different['different_fields']==['crt_state']
        with pytest.raises(RuntimeError,match='missing required'):
            compare_passive_controls(capture('stock',omit_crt=True),capture('runtime',omit_crt=True))
        reference, candidate=capture('stock'),capture('runtime')
    if sample_version == 5:
        different=compare_passive_controls(reference,capture('runtime',wrong_ground=True,name='wrong-ground'))
        assert different['first_observation']==50 and different['different_fields']==['ground_lifecycle']
        with pytest.raises(RuntimeError,match='ground lifecycle is missing or invalid'):
            compare_passive_controls(capture('stock',omit_ground=True),capture('runtime',omit_ground=True))
        reference, candidate=capture('stock'),capture('runtime')
    original=json.loads(candidate.read_text())
    current=json.loads(candidate.read_text())
    current["identities"]["runtime"]="new-runtime"
    current["loaded_runtime"]["sha256"]="new-runtime"
    candidate.write_text(json.dumps(current))
    reused=compare_passive_controls(reference,candidate)
    assert reused["identities"]["runtime"]=="new-runtime"
    assert reused["reference_identities"]["runtime"]=="runtime"
    assert reused["native_runtime_absence_verified"]
    current["loaded_runtime"]["sha256"]="wrong-runtime"
    candidate.write_text(json.dumps(current))
    with pytest.raises(RuntimeError,match="mixed loaded identities"): compare_passive_controls(reference,candidate)
    current=json.loads(json.dumps(original));current["identities"]["observer"]="new-observer"
    current["loaded_observer"]["sha256"]="new-observer"
    candidate.write_text(json.dumps(current))
    with pytest.raises(RuntimeError,match="different dependencies"): compare_passive_controls(reference,candidate)
    candidate.write_text(json.dumps(original))
    repeat = capture("stock", name="stock-repeat")
    assert compare_passive_controls(reference, repeat, candidate_mode="stock")["scope"] == "stock_trajectory_repeatability"
    with pytest.raises(RuntimeError, match="independent"):
        compare_passive_controls(reference, reference, candidate_mode="stock")
    with pytest.raises(RuntimeError, match="execution"):
        compare_passive_controls(reference, candidate, candidate_mode="stock")
    result = compare_passive_controls(reference, capture("runtime", wrong_clock=True))
    assert result["first_observation"] == 50 and result["different_fields"] == ["frame"]
    result = compare_passive_controls(reference, capture("runtime", wrong_vital=True))
    assert result["first_observation"] == 50 and result["different_fields"] == ["p0_vital"]
    reference = capture("stock", boundaries=True)
    result = compare_passive_controls(reference, capture("runtime", boundaries=True))
    assert result["native_boundaries_compared"] == 360
    result = compare_passive_controls(reference, capture("runtime", boundaries=True, wrong_boundary_order=True))
    assert result["first_native_boundary"] == 148 and result["different_fields"] == ["phase"]
    with pytest.raises(RuntimeError, match="opening source"):
        compare_passive_controls(capture("stock", start=20), capture("runtime", start=20))
    with pytest.raises(RuntimeError, match="another request"):
        compare_passive_controls(capture("stock"), capture("runtime", wrong_request=True))

    reference, candidate = capture("stock"), capture("runtime")
    report = json.loads(candidate.read_text())
    log = Path(report["raw_log"]["path"])
    timing_line = (f"\n[ReplayQualification] trajectory timing run_id={report['run_id']} "
                  "viewport_frames=119 native_ticks=119 elapsed_us=2000000 "
                  "viewport_fps_milli=59500 tick_rate_milli=59500 "
                  "max_observation_gap_us=17000 gaps_over_20ms=0\n")
    log.write_text(log.read_text() + timing_line)
    from tools.deterministic_qualification.replay_fidelity import validate_trajectory_timing
    report["timing"] = validate_trajectory_timing(timing_line, report["run_id"])
    report["raw_log"]["sha256"] = sha256_file(log)
    candidate.write_text(json.dumps(report))
    assert compare_passive_controls(reference, candidate)["pacing"]["candidate"]["tick_rate_milli"] == 59500
    report["timing"]["tick_rate_milli"] = 60000
    candidate.write_text(json.dumps(report))
    with pytest.raises(RuntimeError, match="timing report differs"):
        compare_passive_controls(reference, candidate)


def test_source_extent_and_timing_witnesses_are_bounded():
    from tools.deterministic_qualification.replay_fidelity import validate_source_extents, validate_trajectory_timing
    extent = ("[ReplayQualification] source extent run_id=r rounds=2 round=0 slot=0 recorded_time=10 source_samples=10\n"
              "[ReplayQualification] source extent run_id=r rounds=2 round=1 slot=0 recorded_time=0 source_samples=20\n")
    assert validate_source_extents(extent, "r") == [[10], [20]]
    for invalid in (extent.replace("round=1", "round=0"), extent.replace("slot=0", "slot=1"),
                    extent.replace("recorded_time=10", "recorded_time=11")):
        with pytest.raises(RuntimeError):
            validate_source_extents(invalid, "r")
    timing = ("[ReplayQualification] trajectory timing run_id=r viewport_frames=120 native_ticks=120 "
              "elapsed_us=2000000 viewport_fps_milli=60000 tick_rate_milli=60000 "
              "max_observation_gap_us=17000 gaps_over_20ms=0\n")
    assert validate_trajectory_timing(timing, "r")["tick_rate_milli"] == 60000
    with pytest.raises(RuntimeError):
        validate_trajectory_timing(timing.replace("elapsed_us=2000000", "elapsed_us=3000000"), "r")


def test_world_resume_timing_keeps_slow_playback_and_rejects_incomplete_evidence():
    from tools.deterministic_qualification.replay_fidelity import validate_world_resume_timing
    timing = ("[ReplayQualification] world resume timing run_id=r native_ticks=120 elapsed_us=2000000 "
              "viewport_frames=120 max_observation_gap_us=17000 gaps_over_20ms=0\n")
    assert validate_world_resume_timing(timing, "r")["tick_rate_milli"] == 60000
    slow = validate_world_resume_timing(timing.replace("elapsed_us=2000000", "elapsed_us=3000000"), "r")
    assert slow["tick_rate_milli"] == 40000
    for invalid in (timing + timing, timing.replace("native_ticks=120", "native_ticks=119"),
                    timing.replace("run_id=r", "run_id=other"), timing.replace("elapsed_us=2000000", "elapsed_us=0"),
                    timing.replace("viewport_frames=120", "viewport_frames=120 viewport_frames=120"),
                    timing.replace("max_observation_gap_us=17000", "max_observation_gap_us=3000000")):
        with pytest.raises(RuntimeError):
            validate_world_resume_timing(invalid, "r")


@pytest.mark.parametrize("tick,marker", [(360, "interior pause held"), (1010, "application pause held")])
def test_resume_window_includes_first_tick_delay_at_selected_hold(tick, marker):
    from datetime import datetime, timedelta
    from tools.replay_test import interior_resume_timing
    start = datetime(2026, 9, 7)
    held = f"[{start}] [ReplayQualification] {marker} run_id=r native_frame={tick} unchanged=true\n"
    def observations(delay):
        return "".join(f"[{start + timedelta(seconds=i / 60 + delay)}] [ReplayQualification] "
            f"setup ordinal={i} phase=engine_post sample_version=2 frame={tick + i} \n" for i in range(1, 121))
    kwargs = {"held_tick": tick, "marker": marker}
    assert interior_resume_timing(held + observations(0), "r", **kwargs)["tick_rate_milli"] == 60000
    assert interior_resume_timing(held + observations(1), "r", **kwargs)["tick_rate_milli"] == 40000
    with pytest.raises(RuntimeError):
        interior_resume_timing(held + observations(0), "r", held_tick=tick + 1, marker=marker)


def test_completed_application_hold_requires_distinct_ordered_evidence():
    from tools.replay_test import validate_application_pause, validate_interior_pause
    raw = ("application pause started run_id=r native_frame=1005 epoch=400 applications=361 task_retired=true\n"
           "scheduler kind run_id=r vtable_rva=123 ticks=2 interval_ticks=0 prerequisites=1 pending_tasks=0\n"
           "scheduler inventory run_id=r levels=1 ticks=2 kinds=1 active=2 disabled=0 cooldown=0 newly_spawned=0 prerequisites=1 outside_prerequisites=0 pending_tasks=0\n"
           "scheduler bindings validated run_id=r native_frame=1005 strong=1 weak=2 foreign_bindings_rejected=true\n"
           "scheduler prepared run_id=r patches=6 allocations=3 bytes=4096 zero_budget_rejected=true live_unchanged=true never_installed=true\n"
           "scheduler backing replaced run_id=r native_frame=1005 epoch=400 bytes=4096 elapsed_us=300 capacity_rejected=true cancellation_undone=true committed=true unchanged=true\n"
           "scheduler captured run_id=r levels=1 ticks=2 prerequisites=1 bytes=1024 zero_budget_rejected=true\n"
           "vfx requests captured run_id=r native_frame=1005 particles=2 debris=0 bytes=416 independent_storage=true zero_budget_rejected=true\n"
           "application checkpoint captured run_id=r native_frame=1005 bytes=2048 timers=6 borrowed_task=false\n"
           "application pause held run_id=r native_frame=1005 epoch=400 elapsed_us=1000000 application_updates=600 "
           "surface_frames=58 surface_bytes=2359296 applications=361 unchanged=true task_retired=true\n"
           "application checkpoint retained run_id=r captured_tick=1005 resumed_tick=1006 epoch_advanced=true storage_unchanged=true borrowed_task=false\n"
           "application pause resumed run_id=r held_tick=1005 native_callbacks=4\n"
           "application pause started run_id=r native_frame=1010 epoch=420 applications=381 task_retired=true\n"
           "scheduler bindings validated run_id=r native_frame=1010 strong=1 weak=2 foreign_bindings_rejected=true\n"
           "application historical restore rejected run_id=r captured_tick=1005 held_tick=1010 old_epoch=400 current_epoch=420 unchanged=true\n"
           "scheduler historical install cancelled run_id=r captured_tick=1005 held_tick=1010 old_epoch=400 current_epoch=420 elapsed_us=500 changed_before=true target_observed=true current_recovered=true epoch_preserved=true\n"
           "application pause held run_id=r native_frame=1010 epoch=420 elapsed_us=1000000 application_updates=600 "
           "surface_frames=58 surface_bytes=2359296 applications=381 unchanged=true task_retired=true\n"
           "application reentry resumed run_id=r held_tick=1010 native_callbacks=4 storage_unchanged=true\n"
           "scheduler prepared storage released run_id=r unchanged=true never_installed=true\n"
           "scheduler scene lease released run_id=r strong=1 weak=1\n")
    for ordinal, target in enumerate((1005, 1010), 1):
        start = f"application pause started run_id=r native_frame={target} "
        boundary = (f"boundary ordinal={ordinal} phase=round_sequence sample_version=2 frame={target} "
                    "cursor=160 source=1 source_active=true manager_phase=2 move_state=0 round_state=2 round_frame=41 world_mode=2\n")
        raw = raw.replace(start, boundary + start)
    assert validate_application_pause(raw, "r")["completed_applications"] == 361
    shifted = raw.replace("1005", "205").replace("1006", "206").replace("1010", "210")
    assert validate_application_pause(shifted, "r")["reentry"]["tick"] == 210
    consumer = raw.replace("1005", "210").replace("1006", "211").replace("1010", "217")
    assert validate_application_pause(consumer, "r", consumer_task=True)["reentry"]["tick"] == 217
    for wrong in (raw, shifted, consumer.replace("217", "218")):
        with pytest.raises(RuntimeError):
            validate_application_pause(wrong, "r", consumer_task=True)
    with pytest.raises(RuntimeError):
        validate_application_pause(consumer, "r")
    with pytest.raises(RuntimeError, match="active-combat"):
        validate_application_pause(raw.replace("source_active=true", "source_active=false"), "r")
    with pytest.raises(RuntimeError):
        validate_interior_pause(raw, "r")
    for invalid in (raw + raw, raw.replace("epoch=400 elapsed", "epoch=401 elapsed"),
                    raw.replace("applications=361 unchanged", "applications=362 unchanged"),
                    raw.replace("native_callbacks=4", "native_callbacks=0"),
                    raw.replace("surface_frames=58", "surface_frames=0"),
                    raw.replace("task_retired=true", "task_retired=false"),
                    raw.replace("storage_unchanged=true", "storage_unchanged=false"),
                    raw.replace("resumed_tick=1006", "resumed_tick=1005"),
                    raw.replace("borrowed_task=false", "borrowed_task=true"),
                    raw.replace("kinds=1 active", "kinds=2 active"),
                    raw.replace("ticks=2 prerequisites=1 bytes", "ticks=3 prerequisites=1 bytes"),
                    raw.replace("bytes=1024 zero_budget", "bytes=0 zero_budget"),
                    raw.replace("zero_budget_rejected=true", "zero_budget_rejected=false"),
                    raw.replace("current_epoch=420", "current_epoch=400"),
                    raw.replace("application reentry resumed", "missing reentry resume"),
                    raw.replace("released run_id=r strong=1 weak=1", "released run_id=r strong=1 weak=2"),
                    raw.replace("foreign_bindings_rejected=true", "foreign_bindings_rejected=false"),
                    raw.replace("patches=6", "patches=5"),
                    raw.replace("prepared storage released", "prepared storage lost"),
                    raw.replace("cancellation_undone=true", "cancellation_undone=false"),
                    raw.replace("epoch=400 bytes=4096", "epoch=420 bytes=4096"),
                    raw.replace("scheduler backing replaced", "scheduler backing missing"),
                    raw.replace("scheduler historical install cancelled", "historical install missing"),
                    raw.replace("target_observed=true", "target_observed=false"),
                    raw.replace("vfx requests captured", "vfx capture missing"),
                    raw.replace("particles=2 debris=0 bytes=416", "particles=2 debris=0 bytes=415"),
                    raw.replace("current_recovered=true", "current_recovered=false"),
                    raw.replace("epoch_preserved=true", "epoch_preserved=false"),
                    "\n".join(reversed(raw.splitlines()))):
        with pytest.raises(RuntimeError):
            validate_application_pause(invalid, "r")


def test_full_trajectory_requires_complete_source_bound_outcomes():
    from tools.deterministic_qualification.replay_fidelity import validate_trajectory_completion
    raw = ("trajectory completed run_id=run observations=8000 full_match=true\n"
           "native match finished run_id=run boundary=ReplayBattleScene.OnFinishMatch.post\n"
           "native replay endpoint run_id=run native_frame=8123 round=4 cursor=20 world_mode=10 round_state=10 source_active=false outer_complete=true\n"
           "ordered round outcomes verified rounds=5 match_winner=0 winners=1,0,1,0,0\n"
           "[ReplayQualification] recorded match outcome run_id=run recorded=0 simulated=0 equal=true\n")
    result = validate_trajectory_completion(raw, "run", 36000, True)
    assert result["observations"] == 8000 and result["round_winners"] == [1, 0, 1, 0, 0]
    for invalid in (raw.replace("native match finished", "winner known"),
                    raw.replace("outer_complete=true", "outer_complete=false"),
                    raw.replace("observations=8000", "observations=36001"),
                    raw.replace("rounds=5", "rounds=6"),
                    raw.replace("equal=true", "equal=false"),
                    raw.replace("full_match=true", "full_match=false")):
        with pytest.raises(RuntimeError):
            validate_trajectory_completion(invalid, "run", 36000, True)
    with pytest.raises(RuntimeError):
        validate_trajectory_completion(raw, "another-run", 36000, True)


def test_payload_handoff_requires_one_complete_source_bound_observation():
    from tools.deterministic_qualification.replay_fidelity import validate_payload_handoff
    row = (f"[ReplayQualification] payload handoff run_id=run battle_sha256={'a' * 64} "
           f"expected_recording_sha256={'b' * 64} actual_recording_sha256={'b' * 64} "
           "source_equal=true round=0 cursor=0")
    assert validate_payload_handoff(row, "run")["recording_sha256"] == 'b' * 64
    for invalid in ("", row + "\n" + row, row.replace("run_id=run", "run_id=stale"),
                    row.replace("source_equal=true", "source_equal=false"),
                    row.replace("actual_recording_sha256=" + 'b' * 64, "actual_recording_sha256=" + 'c' * 64),
                    row.replace("battle_sha256=" + 'a' * 64, "battle_sha256=short"),
                    row.replace("cursor=0", "cursor=1"), row + " source_equal=true"):
        with pytest.raises(RuntimeError, match="payload handoff"):
            validate_payload_handoff(invalid, "run")

    from tools.deterministic_qualification.replay_fidelity import validate_recorded_outcome
    result = "[ReplayQualification] recorded match outcome run_id=run recorded=1 simulated=1 equal=true"
    assert validate_recorded_outcome(result, "run", 1)["recorded_match_winner"] == 1
    for invalid in ("", result + "\n" + result, result.replace("run_id=run", "run_id=stale"),
                    result.replace("recorded=1", "recorded=0"), result.replace("equal=true", "equal=false")):
        with pytest.raises(RuntimeError, match="recorded outcome"):
            validate_recorded_outcome(invalid, "run", 1)
    from tools.deterministic_qualification.replay_fidelity import validate_initial_snapshot
    snapshot = ("[ReplayQualification] initial round snapshot run_id=run version=1 round=0 cursor=0 "
                "available=true consumed_equal=true raw_equal=false expected_motion_count=0 "
                "actual_motion_count=0 consumed_difference=4294967295")
    assert validate_initial_snapshot(snapshot, "run")["raw_equal"] is False
    for invalid in (snapshot.replace("consumed_equal=true", "consumed_equal=false"),
                    snapshot.replace("expected_motion_count=0", "expected_motion_count=17"),
                    snapshot.replace("actual_motion_count=0", "actual_motion_count=1"),
                    snapshot.replace("version=1", "version=0")):
        with pytest.raises(RuntimeError, match="initial snapshot"):
            validate_initial_snapshot(invalid, "run")


@pytest.mark.parametrize("version", (2,3))
def test_intro_skip_requires_native_intro_before_source_activation(version):
    from tools.replay_test import validate_intro_skips
    raw = (f"boundary ordinal=80 phase=round_sequence sample_version={version} frame=21 round=0 cursor=0 "
           "source_active=false manager_phase=2 round_state=6 world_mode=6\n"
           "intro skip request run_id=r native_frame=21 world_mode=6 source_active=false ready=true\n"
           "setup completed run_id=r observations=40 native_frame=44\n")
    assert validate_intro_skips(raw, "r")["source_activation_tick"] == 44
    for invalid in (raw.replace("world_mode=6", "world_mode=2"),
                    raw.replace("source_active=false", "source_active=true"),
                    raw.replace("native_frame=44", "native_frame=20"),
                    raw.replace("ready=true", "ready=false"), raw + raw):
        with pytest.raises(RuntimeError):
            validate_intro_skips(invalid, "r")


def test_trace_causality_gate_requires_every_boundary_and_retains_first_difference(tmp_path):
    import json
    from tools.replay_test import compare_same_process_trace, sha256_file
    raw = tmp_path / "trace.log"
    report = tmp_path / "trace.json"
    lines = [f"trace source run_id=r generation={g} tick={t} player={p} states=1 expired=0 attached=1 "
             f"fading=1 completion=0 pending=0 strong=1 child=0 membership=ab hash=cd native_actor_delta={g}"
             for g in range(2) for t in range(206,211) for p in range(2)]
    def check(rows):
        raw.write_text("\n".join(rows))
        report.write_text(json.dumps({"run_id":"r", "identities":{"runtime":"a"},
                                     "raw_log":{"path":str(raw),"sha256":sha256_file(raw)}}))
        return compare_same_process_trace(report)
    assert check(lines)["result"] == "pass"
    remapped=[line.replace('membership=ab',f'membership={i} membership_version=2 topology=0123456789abcdef') for i,line in enumerate(lines)]
    assert check(remapped)["result"] == "pass"
    wrong=remapped.copy();wrong[10]=wrong[10].replace('topology=0123456789abcdef','topology=1123456789abcdef')
    assert check(wrong)["first_mismatch"]["field"] == "topology"
    wrong=remapped.copy();wrong[10]=wrong[10].replace('membership_version=2','membership_version=3')
    with pytest.raises(RuntimeError,match='membership protocol'):check(wrong)
    wrong=remapped.copy();wrong[10]=wrong[10].replace(' topology=0123456789abcdef','')
    with pytest.raises(RuntimeError,match='membership protocol'):check(wrong)
    bad = lines.copy()
    bad[10] = bad[10].replace("hash=cd", "hash=ef")
    result = check(bad)
    assert result["result"] == "fail"
    assert result["first_mismatch"]["tick"] == 206
    assert result["first_mismatch"]["player"] == 0
    assert check(lines[:-1])["result"] == "incomplete"
    with pytest.raises(RuntimeError, match="duplicate"):
        check(lines + lines[:1])


def test_callback_admission_requires_both_boundaries_and_detach():
    from replay_test import validate_callback_admission
    raw = ("callback checkpoint tick=205 latent=2 streamable=3 policy=empty_registered_v1\n"
           "callback checkpoint tick=210 latent=2 streamable=3 policy=empty_registered_v1\n"
           "callback admission stopped latent=2 streamable=3 detached=true\n")
    assert validate_callback_admission(raw, 210)["result"] == "pass"
    for invalid in (raw.replace("tick=210", "tick=209"), raw.replace("tick=210 latent=2", "tick=210 latent=4"),
                    raw.replace("detached=true", "detached=false"), raw + "callback lease invalidated"):
        with pytest.raises(RuntimeError):
            validate_callback_admission(invalid, 210)


def test_main_callback_admission_uses_preparation_recovery_receipt():
    import ast
    import replay_test as module
    source=Path(module.__file__).read_text(encoding='utf-8')
    main=next(n for n in ast.parse(source).body if isinstance(n,ast.FunctionDef) and n.name=='main')
    assignment=next(n for n in ast.walk(main) if isinstance(n,ast.Assign)
        and isinstance(n.value,ast.Call) and isinstance(n.value.func,ast.Name)
        and n.value.func.id=='validate_callback_admission')
    raw=('callback checkpoint tick=205 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n'
         'host seek accepted run_id=c checkpoint=205 origin=210 target=208 aliases_rejected=true\n'
         'historical cancellation injected run_id=c point=before tick=210 preparation_pending=true render_command_pending=true gpu_completion_pending=false\n'
         'restore preparation failed participant=preparation_cancelled code=31 target=205 original=210 before_publication=true\n'
         'restore preparation recovery completed original=210 observed=210 ground_released=true render_released=true before_A_publication=true\n'
         'historical cancellation recovered run_id=c point=before tick=210\n'
         'callback admission stopped latent=0 streamable=0 detached=true\n')
    scope=dict(summary={},candidate_raw=raw,candidate_report={'run_id':'c'},first_case='before',
        second_undo_captured=False,host_seek_first_target=lambda _:209,
        validate_callback_admission=module.validate_callback_admission,
        options=SimpleNamespace(combat_advanced_tick=210,capture_cancel=False,restore_reuse=False,
            host_seek_repeat=False,seek_preparation_failure=False,checkpoint_pair=False,
            combat_anchor_tick=205,checkpoint_fallback=False,host_seek=True))
    code=compile(ast.Module(body=[assignment],type_ignores=[]),'production_main_callback_call','exec')
    exec(code,scope)
    assert scope['summary']['callback_admission']['preparation_cancellation']['complete_B_captured'] is False
    scope['first_case']='after'
    with pytest.raises(RuntimeError):exec(code,scope)


def test_callback_admission_preparation_cancel_requires_native_recovery():
    from replay_test import validate_callback_admission
    raw=("callback checkpoint tick=6417 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n"
         "host seek accepted run_id=c checkpoint=6417 origin=6419 target=6418 aliases_rejected=true\n"
         "historical cancellation injected run_id=c point=before tick=6419 preparation_pending=true render_command_pending=true gpu_completion_pending=false\n"
         "restore preparation failed participant=preparation_cancelled code=31 target=6417 original=6419 before_publication=true\n"
         "restore preparation recovery completed original=6419 observed=6419 ground_released=true render_released=true before_A_publication=true\n"
         "historical cancellation recovered run_id=c point=before tick=6419\n"
         "callback admission stopped latent=0 streamable=0 detached=true\n")
    check=lambda text:validate_callback_admission(text,6419,anchor_tick=6417,preparation_cancel_run='c')
    result=check(raw)
    assert result['preparation_cancellation']['complete_B_captured'] is False
    assert result['preparation_cancellation']['original_tick']==6419
    for bad in (raw.replace('observed=6419','observed=6418'),raw.replace('ground_released=true','ground_released=false'),
                raw.replace('run_id=c point=before tick=6419\n','run_id=wrong point=before tick=6419\n'),
                raw.replace('preparation_cancelled','preparation_domain'),raw.replace('detached=true','detached=false'),
                raw+'host seek publication observed run_id=c\n',raw+'restore request prepared target=6417\n',
                raw.replace('restore preparation recovery completed','missing recovery receipt'),
                raw.replace('host seek accepted','missing request'),raw+raw):
        with pytest.raises(RuntimeError):check(bad)
    with pytest.raises(RuntimeError):validate_callback_admission(raw,6419,anchor_tick=6417)
    undo=('callback checkpoint tick=6419 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n'
          'restore undo captured target=6417 original=6419 gpu_complete=true before_target_preparation=true owned_bytes=100\n')
    captured=raw.replace('historical cancellation injected',undo+'historical cancellation injected')
    assert check(captured)['preparation_cancellation']['complete_B_captured'] is True
    with pytest.raises(RuntimeError):check(captured.replace('callback checkpoint tick=6419','callback checkpoint tick=6418'))


def test_callback_admission_checks_recapture_and_final_B_stamp():
    from replay_test import validate_callback_admission
    raw=("callback checkpoint tick=205 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n"*2
         +"callback checkpoint tick=210 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n"
         +"callback admission stopped latent=0 streamable=0 detached=true\n")
    assert validate_callback_admission(raw,210,True)["result"]=="pass"
    reused=raw.replace("callback admission stopped", "callback checkpoint tick=210 latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\ncallback admission stopped")
    assert validate_callback_admission(reused,210,True,True)["result"]=="pass"
    import pytest
    pair="".join(f"callback checkpoint tick={tick} latent=0 streamable=0 domain=7 policy=quiescent_streamable_v2\n" for tick in (205,206,211,210))+"callback admission stopped latent=0 streamable=0 detached=true\n"
    assert validate_callback_admission(pair,211,repeat_seek=True,repeat_origin=210,checkpoint_pair=True)["result"]=="pass"
    with pytest.raises(RuntimeError): validate_callback_admission(pair.replace("tick=206","tick=205"),211,repeat_seek=True,repeat_origin=210,checkpoint_pair=True)
    with pytest.raises(RuntimeError): validate_callback_admission(raw,210)
    with pytest.raises(RuntimeError): validate_callback_admission(raw.replace("tick=210 latent=0","tick=210 latent=1"),210,True)


def test_widget_continuation_rejects_wrong_history_despite_matching_delta():
    from replay_test import compare_widget_continuation
    native = "HUD clock tick=211 widget=DmgValueEff_C player=0 delta=0.016666668 slate_delta=0.016 before=0.066666670 after=0.083333338 read_only=true"
    # Wall time may differ; sequence history may not.
    assert compare_widget_continuation(native, native.replace("slate_delta=0.016", "slate_delta=0.125"), 210)["active_player_updates"] == 1
    wrong = native.replace("before=0.066666670", "before=0.166666670").replace("after=0.083333338", "after=0.183333338")
    with pytest.raises(RuntimeError, match="differs"):
        compare_widget_continuation(native, wrong, 210)
    with pytest.raises(RuntimeError, match="coverage"):
        compare_widget_continuation(native, "", 210)
    with pytest.raises(RuntimeError, match="differs"):
        compare_widget_continuation(native, native+"\n"+native, 210)


def test_widget_continuation_requires_explicit_inactive_hud_coverage():
    from replay_test import compare_widget_continuation
    rows=[f"[ReplayQualification] HUD state tick={tick} p1=3f800000 p2=3f800000 combo1=3f800000 combo2=3f800000 timer=00000000 damage=0 types=0 pool=8 active=0 read_only=true" for tick in range(2505,2515)]
    native="\n".join(rows)
    result=compare_widget_continuation(native,native,2504)
    assert result["active_player_updates"]==0 and result["inactive_player_observations"]==10
    for wrong in ("", "\n".join(rows[:-1]), native+"\n"+rows[0], native.replace("p1=3f800000","p1=00000000"), native.replace("active=0","active=1")):
        with pytest.raises(RuntimeError): compare_widget_continuation(native,wrong,2504)
    active=native.replace("active=0","active=1")
    with pytest.raises(RuntimeError,match="coverage"): compare_widget_continuation(active,active,2504)
    with pytest.raises(RuntimeError,match="coverage"): compare_widget_continuation("","",2504)


def test_complete_B_failure_defers_control_until_intended_target_rejection():
    from replay_test import validate_complete_B_target_preparation
    markers=("host seek repeated run_id=c",
        "restore undo captured target=205 original=220 gpu_complete=true before_target_preparation=true",
        "lighting missing target primitive captured_id=912",
        "host seek preparation failure observed run_id=c tick=220 unchanged_B=true before_publication=true")
    validate_complete_B_target_preparation("\n".join(markers),"c")
    for rows in (markers[:1]+markers[2:],(markers[0],markers[2],markers[1],markers[3]),markers[:-1]):
        with pytest.raises(RuntimeError,match="native control deferred"):
            validate_complete_B_target_preparation("\n".join(rows),"c")
    with pytest.raises(RuntimeError,match="vfx_requests"):
        validate_complete_B_target_preparation("checkpoint component failed component=vfx_requests code=5 tick=220","c")


def test_cancel_control_requires_owned_click_and_accepted_host_dispatch():
    from replay_test import validate_cancel_control
    raw="\n".join(("replay cancel control hwnd=123 screen_x=100 screen_y=160",
        "historical cancel control waiting run_id=c tick=220",
        "replay cancel activated request=1 surface_frame=34",
        "replay UI cancel dispatched tick=220 accepted=true phase=9"))
    report={"run_id":"c","loaded_observer":{"pid":7},
        "ui_cancel_input":{"method":"Windows SendInput","button_released":True,"pid":7,"hwnd":123,"x":100,"y":160}}
    assert validate_cancel_control(raw,report)["dispatch"]=="host_ui"
    for invalid in (raw.replace("accepted=true","accepted=false"),raw.replace("phase=9","phase=11"),
                    raw.replace("request=1","request=2"),raw+"\nreplay cancel activated request=2 surface_frame=35"):
        with pytest.raises(RuntimeError): validate_cancel_control(invalid,report)
    with pytest.raises(RuntimeError): validate_cancel_control(raw,dict(report,ui_cancel_input={}))
    with pytest.raises(RuntimeError): validate_cancel_control(raw,dict(report,loaded_observer={"pid":8}))


def test_step_control_requires_owned_click_and_pending_boundary_observation():
    from replay_test import validate_step_control
    raw = "\n".join(("replay step control hwnd=123 screen_x=100 screen_y=120",
        "historical step control waiting run_id=c tick=208 pending_event=true unchanged=true",
        "replay step activated request=1 surface_frame=34",
        "historical step control verified run_id=c tick=208 requests=1 pending_event=true unchanged=true",
        "historical single step requested run_id=c origin=208 target=209"))
    report = {"run_id":"c", "loaded_observer":{"pid":7},
        "ui_step_input":{"method":"Windows SendInput","button_released":True,"pid":7,"hwnd":123,"x":100,"y":120}}
    assert validate_step_control(raw,report)["target"] == 209
    autonomous=raw+" dispatch=host_ui\nreplay UI step dispatched origin=208 target=209 phase=6 observer=false\nhost step monitor retired run_id=c tick=209"
    assert validate_step_control(autonomous,report)["dispatch"]=="host_ui"
    for invalid in (autonomous.replace("phase=6","phase=5"),autonomous.replace("host step monitor retired","missing monitor retirement")):
        with pytest.raises(RuntimeError): validate_step_control(invalid,report)
    for invalid in (raw.replace("request=1", "request=2"),raw.replace("pending_event=true", "pending_event=false")):
        with pytest.raises(RuntimeError): validate_step_control(invalid,report)
    with pytest.raises(RuntimeError): validate_step_control(raw,dict(report,ui_step_input={}))
    with pytest.raises(RuntimeError): validate_step_control(raw,dict(report,loaded_observer={"pid":8}))


def test_full_index_requires_each_independent_native_boundary_and_retirement(monkeypatch):
    import replay_test
    native=[dict(frame="1",phase="round_sequence",round="0",round_frame="1",cursor="4",round_state="2",source_active="true",move_state="0"),
            dict(frame="2",phase="round_sequence",round="0",round_frame="2",cursor="4",round_state="2",source_active="true",move_state="0")]
    monkeypatch.setattr(replay_test,"validate_boundary_capture",lambda *_:native)
    candidate="index started run_id=c entries=1 bytes=1024 native_tick=0\n"
    for tick in range(3):
        candidate+=f"[ReplayQualification] index row tick={tick} native_tick={tick} interval={1 if tick else 0} publications={1 if tick else 0} round=0 round_tick={tick} cursor=4 phase={9 if tick else 0} round_state=2 source_active=1 move_state=0\n"
    candidate+="replay index final phase=2 entries=3 intervals=2 empty=1 multi=1 bytes=1024 native_finish=true final_tail=true\nindex complete run_id=c entries=3 bytes=1024 intervals=2 empty=1 multi=1 native_finish=true final_tail=true\nindex released run_id=c bytes=0 hooks_removed=true"
    native.extend([dict(native[-1],frame=str(tick)) for tick in (3,4)])
    control="native replay endpoint run_id=n native_frame=2\nindex terminal observed run_id=n replay_tick=2 frozen=true\nindex closure run_id=n replay_tick=2 native_tick=4 callbacks_retained=true"
    candidate+="\nindex terminal observed run_id=c replay_tick=2 frozen=true\nindex closure run_id=c replay_tick=2 native_tick=4 callbacks_retained=true"
    candidate+="\n[HorseMod] index closure replay_tick=2 executor_tick=3 application_idle=true"
    result=replay_test.validate_tick_index(control,candidate,"n","c")
    assert result["index_complete"] and result["native_boundary_comparisons"]==2
    assert not result["arbitrary_seek_complete"]
    with pytest.raises(RuntimeError,match="independent native control"):
        replay_test.validate_tick_index(control,candidate,"c","c")
    for broken in (candidate.replace("tick=1 native_tick=1","tick=2 native_tick=2"),
                   candidate.replace("round_tick=2","round_tick=9"),
                   candidate.replace("hooks_removed=true","hooks_removed=false"),
                   candidate.replace("executor_tick=3","executor_tick=5"),
                   candidate.replace("application_idle=true","application_idle=false"),
                   candidate.replace("entries=3","entries=2")):
        with pytest.raises(RuntimeError): replay_test.validate_tick_index(control,broken,"n","c")
    native.pop()
    with pytest.raises(RuntimeError,match="callbacks were dropped"):
        replay_test.validate_tick_index(control,candidate,"n","c")


@pytest.mark.parametrize("original", [None,b"user disabled overlay\r\n"])
def test_index_checkpoint_overlay_is_enabled_and_recovers_exact_marker(tmp_path,monkeypatch,original):
    from tools.deterministic_qualification.replay_control import prepare_replay_overlay
    from tools.deterministic_qualification.run_resources import RunResources
    from tools.deterministic_qualification import process_control
    monkeypatch.setattr(process_control,"list_game_processes",lambda:[])
    game=tmp_path/"SoulcaliburVI.exe";game.write_bytes(b"fixture")
    marker=tmp_path/"disable_gameimgui.txt"
    if original is not None:marker.write_bytes(original)
    journal=RunResources(tmp_path/"resources.json");journal.begin("index-overlay-test")
    assert prepare_replay_overlay(journal,game,{"index_checkpoint":True})
    assert not marker.exists()
    journal.restore()
    assert (marker.read_bytes() if marker.exists() else None)==original
    assert not prepare_replay_overlay(journal,game,{"record_index":True})
    assert (marker.read_bytes() if marker.exists() else None)==original


@pytest.mark.parametrize("present", [False,True])
def test_loader_temporary_activation_restores_original_state(tmp_path,monkeypatch,present):
    from tools.deterministic_qualification import replay_control, process_control
    from tools.deterministic_qualification.run_resources import RunResources
    # This fixture owns only temporary files. Process-ownership rejection is
    # tested independently; do not couple file recovery to the user's game.
    monkeypatch.setattr(process_control,"list_game_processes",lambda:[])
    game=tmp_path/"SoulcaliburVI.exe";game.write_bytes(b"fixture")
    disabled=tmp_path/"dwmapi.dll.DISABLED";disabled.write_bytes(b"verified shim fixture")
    monkeypatch.setattr(replay_control,"DISABLED_REPLAY_LOADER_SHA256",sha256_file(disabled))
    target=tmp_path/"dwmapi.dll"
    if present:target.write_bytes(b"original enabled shim")
    journal=RunResources(tmp_path/"resources.json");journal.begin("loader-test")
    result=replay_control.prepare_replay_loader(journal,game)
    assert result["temporary"]== (not present)
    assert target.read_bytes()==(b"original enabled shim" if present else disabled.read_bytes())
    journal.restore()
    assert disabled.read_bytes()==b"verified shim fixture"
    assert (target.read_bytes() if target.exists() else None)==(b"original enabled shim" if present else None)


def test_loader_unknown_disabled_identity_rejects_before_publication(tmp_path):
    from tools.deterministic_qualification.replay_control import prepare_replay_loader
    (tmp_path/"dwmapi.dll.DISABLED").write_bytes(b"unknown")
    with pytest.raises(RuntimeError,match="identity is not verified"):
        prepare_replay_loader(None,tmp_path/"SoulcaliburVI.exe")
    assert not (tmp_path/"dwmapi.dll").exists()


def test_index_endpoint_excludes_next_publication_at_same_tick():
    from tools.deterministic_qualification.replay_fidelity import completed_replay_boundary_prefix
    tail = dict(phase="actor_tail", frame="11501", world_mode="10", round_state="10", source_active="false")
    rows = [dict(tail, phase="input_cache_publication", frame="11500"), tail,
            dict(tail, phase="input_cache_publication"), dict(tail, frame="11502")]
    prefix, remaining = completed_replay_boundary_prefix(rows, 11501)
    assert prefix == rows[:2] and remaining == 2
    for broken in (rows[:1] + rows[2:], rows + [tail],
                   [dict(row, source_active="true") for row in rows]):
        with pytest.raises(RuntimeError, match="unique completed terminal interval"):
            completed_replay_boundary_prefix(broken, 11501)


def test_retained_source_endpoint_requires_explicit_mode_and_complete_tail():
    from tools.deterministic_qualification.replay_fidelity import completed_replay_boundary_prefix
    tail = dict(phase="actor_tail", frame="11148", world_mode="5", round_state="5", source_active="false")
    rows = [dict(tail, phase="input_cache_publication", frame="11147"), tail,
            dict(tail, phase="input_cache_publication")]
    with pytest.raises(RuntimeError, match="unique completed terminal interval"):
        completed_replay_boundary_prefix(rows, 11148)
    assert completed_replay_boundary_prefix(rows, 11148, retained_source_stop=True)==(rows[:2],1)
    for broken in (rows[:1]+rows[2:], rows+[tail],
                   [dict(row, source_active="true") for row in rows],
                   [dict(row, world_mode="10",round_state="10") for row in rows]):
        with pytest.raises(RuntimeError, match="unique completed terminal interval"):
            completed_replay_boundary_prefix(broken,11148,retained_source_stop=True)


def test_index_checkpoint_waits_for_native_resource_retirement():
    from replay_test import validate_index_checkpoint_retention
    raw="\n".join((
        "index checkpoint owned tick=170 retained=1 owned_bytes=120000000",
        "index checkpoint retained run_id=c tick=170 entries=171 elapsed_us=99000 owned_bytes=120000000 callbacks_unchanged=true application_idle=true",
        "index checkpoint capture retired run_id=c tick=170 immutable_owner_retained=true",
        "index complete run_id=c",
        "index checkpoint release witness tick=170 current_tick=12304 vfx_owners_live=false target_shape_supported=true owned_bytes=120000000",
        "seek checkpoint retired tick=12304 application_idle=true",
        "index released run_id=c bytes=0 hooks_removed=true"))
    result=validate_index_checkpoint_retention(raw,"c")
    assert result["checkpoint_resources_retired"] and not result["vfx_owners_live_at_release"]
    assert not result["cross_round_restore_proven"] and not result["full_checkpoint_coverage"]
    for broken in (raw.replace("seek checkpoint retired","missing retirement"),
                   raw.replace("120000000","1073741825"),raw.replace("callbacks_unchanged=true","callbacks_unchanged=false"),
                   raw.replace("retained=1","retained=2"),raw+"\nindex released run_id=c bytes=0 hooks_removed=true"):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(broken,"c")


def test_held_index_cancellation_requires_retirement_before_resume(monkeypatch):
    import replay_test
    rows=[dict(phase="actor_tail",frame=str(tick)) for tick in range(171,291)]
    monkeypatch.setattr(replay_test,"validate_boundary_capture",lambda raw,identity:rows)
    raw="\n".join((
        "index held cancellation queued run_id=c tick=170 entries=171 bytes=4194288 complete=false",
        "seek checkpoint retired tick=170 application_idle=true",
        "index held cancellation completed run_id=c tick=170 entries=0 bytes=0 callbacks_unchanged=true epoch_unchanged=true application_unchanged=true"))
    assert replay_test.validate_held_index_cancellation(raw,"c")["retired_while_held"]
    for bad in (raw.replace("retired tick=170","retired tick=171"),raw.replace("epoch_unchanged=true","epoch_unchanged=false"),
                raw+"\nindex complete run_id=c",raw.replace("bytes=4194288","bytes=1073741825"),
                "\n".join(reversed(raw.splitlines()))):
        with pytest.raises(RuntimeError):replay_test.validate_held_index_cancellation(bad,"c")
    rows.pop()
    with pytest.raises(RuntimeError,match="120 resumed ticks"):
        replay_test.validate_held_index_cancellation(raw,"c")


def test_private_output_requires_completion_and_unbroken_present_chain():
    from replay_test import validate_private_replay_output
    raw="\n".join((
        "private replay output prepared hwnd=123 bytes=8421632 native_target_untouched=true",
        "private replay output displayed tick=208 hwnd=123 native_target_untouched=true",
        "private replay output retired hwnd=123 gpu_complete=true"))
    assert validate_private_replay_output(raw)["held_ticks"]==[208]
    for invalid in (raw.rsplit("\n",1)[0],raw.replace("tick=208 hwnd=123","tick=208 hwnd=456"),
                    raw+"\nnested Present detected",raw.replace("gpu_complete=true","gpu_complete=false")):
        with pytest.raises(RuntimeError): validate_private_replay_output(invalid)


def test_original_viewport_requires_game_identity_and_completed_restore():
    from replay_test import validate_private_replay_output
    raw="\n".join((
        "DX11 overlay initialised (hwnd=123, dev=4, ctx=5)",
        "native replay viewport prepared hwnd=123 bytes=3686400 swap_effect=0 buffers=1",
        "native replay viewport displayed tick=345 hwnd=123 backbuffer_restored=true gpu_complete=true",
        "native replay viewport transaction finished commit=true suppressed_presents=7 gpu_complete=true",
        "native replay viewport retired hwnd=123 gpu_complete=true"))
    result=validate_private_replay_output(raw)
    assert result['result']=='pass' and result['original_window'] and result['held_ticks']==[345]
    for bad in (raw.replace('initialised (hwnd=123','initialised (hwnd=456'),
                raw.replace('backbuffer_restored=true','backbuffer_restored=false'),
                raw.rsplit('\n',1)[0],raw.replace('swap_effect=0','swap_effect=4'),
                raw+'\nprivate replay output prepared hwnd=456 bytes=99 native_target_untouched=true',
                raw+'\nnative replay viewport completion failed retained=true'):
        with pytest.raises(RuntimeError):validate_private_replay_output(bad)


def test_advance_failure_pose_continuation_uses_executed_generation():
    from replay_test import compare_native_poses
    reports=[{"run_id":"native"},{"run_id":"candidate","seek_advance_failure":True}]
    texts=[]
    for index,report in enumerate(reports):
        lines=[]
        for tick in list(range(185,205))+list(range(221,341)):
            generation=1 if index and tick>220 else 0
            fields=f"run_id={report['run_id']} tick={tick} generation={generation}"
            lines.append(f"native pose publication {fields} ordinal=0 player=0 bones=1 mapping=a hash=b")
            lines.append(f"native pose completed {fields} evaluations=1")
        texts.append("\n".join(lines))
    assert compare_native_poses(texts,reports,220,"after")["result"]=="pass"
    reports[1]["seek_advance_failure"]=False
    with pytest.raises(RuntimeError,match="missing native pose continuation"):
        compare_native_poses(texts,reports,220,"after")


def test_commit_ownership_requires_completed_tails_before_retirement():
    import replay_test
    states=["BRetained","APublished","ExecutionActive","CSettled","CompletingTargetTails","TargetTailsCompleted","Committing","Committed"]
    rows=[]
    for i,state in enumerate(states):
        tick=[220,170,170,214,214,214,214,214][i]
        rows.append(f"seek ownership state={state} B_tick=220 target=214 current_tick={tick} completed_tail_tick={214 if i>=5 else 0} B_retained={'true' if i<6 else 'false'} commit_decided={'false' if i<6 else 'true'} executed_ticks={44 if i>=5 else 0} executed_intervals={44 if i>=5 else 0}")
    raw="\n".join(rows)
    assert replay_test.validate_seek_commit_ownership(raw,170,220,214)['completed_tail_tick']==214
    import pytest
    for bad in (raw.replace(rows[5]+"\n",""),raw.replace("state=CSettled B_tick=220 target=214 current_tick=214","state=CSettled B_tick=220 target=214 current_tick=213"),raw.replace("B_retained=true commit_decided=false","B_retained=false commit_decided=true",1),raw.replace("completed_tail_tick=214","completed_tail_tick=213")):
        with pytest.raises(RuntimeError): replay_test.validate_seek_commit_ownership(bad,170,220,214)


def test_step_commit_retains_B_through_both_external_targets():
    import replay_test
    states=["BRetained","APublished","ExecutionActive","CSettled","ExecutionActive","CSettled","CompletingTargetTails","TargetTailsCompleted","Committing","Committed"]
    coordinates=[220,170,170,208,208,209,209,209,209,209]
    lines=[]
    for i,state in enumerate(states):
        lines.append(f"seek ownership state={state} B_tick=220 target={208 if i<4 else 209} current_tick={coordinates[i]} completed_tail_tick={209 if i>=7 else 0} B_retained={'true' if i<8 else 'false'} commit_decided={'false' if i<8 else 'true'} executed_ticks={39 if i>=7 else 0} executed_intervals={39 if i>=7 else 0}")
    raw="\n".join(lines)
    assert replay_test.validate_seek_commit_ownership(raw,170,220,208,True)['stepped_target']==209
    for bad in (raw.replace(lines[4]+"\n",""),raw.replace('state=ExecutionActive B_tick=220 target=209','state=ExecutionActive B_tick=220 target=210'),raw.replace(lines[5],lines[5].replace('B_retained=true','B_retained=false'))):
        with pytest.raises(RuntimeError): replay_test.validate_seek_commit_ownership(bad,170,220,208,True)


@pytest.mark.workflow
@pytest.mark.parametrize("intervention", ["--seek-advance-failure", "--seek-settlement-failure"])
def test_cross_round_recovery_cli_reaches_build_without_launch(monkeypatch,tmp_path,intervention):
    import replay_test,sys
    class BuildReached(BaseException): pass
    def stop_before_build(*args): raise BuildReached()
    monkeypatch.setattr(replay_test,"ensure_build",stop_before_build)
    monkeypatch.setattr(replay_test,"OUTPUT",tmp_path)
    monkeypatch.setattr(sys,"argv",["replay_test.py","combat-restore","--host-seek","--combat-anchor-tick","170",
        "--combat-advanced-tick","2504","--combat-case","after","--host-seek-target","214",intervention])
    with pytest.raises(BuildReached): replay_test.main()


@pytest.mark.workflow
@pytest.mark.parametrize("extra,accepted", [(["--seek-settlement-failure"],True),([],False),
    (["--seek-advance-failure"],False),(["--seek-settlement-failure","--combat-case","commit"],False),
    (["--seek-settlement-failure","--interactive-controls"],False)])
@pytest.mark.parametrize("target",["5750","11000"])
def test_midpoint_recovery_cli_is_bounded(monkeypatch,tmp_path,extra,accepted,target):
    import replay_test,sys
    class BuildReached(BaseException): pass
    def stop(*args): raise BuildReached()
    monkeypatch.setattr(replay_test,"ensure_build",stop)
    monkeypatch.setattr(replay_test,"OUTPUT",tmp_path)
    monkeypatch.setattr(sys,"argv",["replay_test.py","combat-restore","--host-seek","--combat-anchor-tick","170",
        "--combat-advanced-tick","2504","--combat-case","after","--host-seek-target",target]+extra)
    with pytest.raises(BuildReached if accepted else SystemExit):replay_test.main()


@pytest.mark.workflow
@pytest.mark.parametrize("extra", [[],["--seek-advance-failure","--combat-case","commit"],["--seek-advance-failure","--changed-inputs"]])
def test_cross_round_214_cli_rejects_unqualified_combinations(monkeypatch,tmp_path,extra):
    import replay_test,sys
    monkeypatch.setattr(replay_test,"OUTPUT",tmp_path)
    monkeypatch.setattr(replay_test,"ensure_build",lambda *_: pytest.fail("invalid protocol reached build"))
    monkeypatch.setattr(sys,"argv",["replay_test.py","combat-restore","--host-seek","--combat-anchor-tick","170",
        "--combat-advanced-tick","2504","--combat-case","after","--host-seek-target","214"]+extra)
    with pytest.raises(SystemExit) as error: replay_test.main()
    assert error.value.code==2


def test_completed_seek_rewind_uses_native_intervals_and_requires_commit_chain():
    import replay_test
    states=["BRetained","APublished","ExecutionActive","CSettled","CompletingTargetTails","TargetTailsCompleted","Committing","Committed"]
    current=[174,170,170,172,172,172,172,172]
    ownership="\n".join(f"seek ownership state={state} B_tick=174 target=172 current_tick={current[i]} completed_tail_tick={172 if i>=5 else 0} B_retained={'true' if i<6 else 'false'} commit_decided={'false' if i<6 else 'true'} executed_ticks={2 if i>=5 else 0} executed_intervals={2 if i>=5 else 0}" for i,state in enumerate(states))
    events=[("round_sequence",171),("actor_tail",171),("actor_tail",171),
            ("round_sequence",172),("round_sequence",173),("actor_tail",173),("round_sequence",174),("actor_tail",174)]
    rows=[f"[ReplayQualification] boundary ordinal={i} phase={phase} sample_version=3 frame={tick} " for i,(phase,tick) in enumerate(events)]
    raw="historical A captured run_id=run tick=170 \n"+"\n".join(rows)+"\nrestore undo captured target=170 original=174 \n"+ownership
    assert replay_test.completed_seek_observed_rewind(raw,"run",170,174,172)==("run","4","4")
    assert replay_test.completed_seek_observed_rewind(raw.replace(rows[2]+"\n",""),"run",170,174,172)==("run","4","3")
    for bad in (raw.replace(rows[3]+"\n",""),raw.replace(rows[5],rows[5].replace("frame=173","frame=175")),
                raw.replace("state=TargetTailsCompleted","state=Skipped"),raw.replace("state=CSettled B_tick=174","state=CSettled B_tick=175"),
                raw+"\nhistorical A captured run_id=run tick=170 "):
        with pytest.raises(RuntimeError): replay_test.completed_seek_observed_rewind(bad,"run",170,174,172)


@pytest.mark.workflow
@pytest.mark.parametrize("extra", [[],["--candidate-only"],["--coherence-images"]])
def test_cross_round_source_correction_admitted_without_launch(monkeypatch,tmp_path,extra):
    import replay_test,sys
    class BuildReached(BaseException): pass
    def stop(*args): raise BuildReached()
    monkeypatch.setattr(replay_test,"ensure_build",stop)
    monkeypatch.setattr(replay_test,"OUTPUT",tmp_path)
    monkeypatch.setattr(sys,"argv",["replay_test.py","combat-restore","--host-seek","--combat-anchor-tick","170",
        "--combat-advanced-tick","2504","--combat-case","commit","--host-seek-target","208",
        "--combat-exact-advance","--corrected-inputs","--source-revision"]+extra)
    with pytest.raises(BuildReached): replay_test.main()


def test_seek_release_acceptance_and_completed_retirement_order():
    from replay_test import validate_seek_release_retirement
    released="host seek released run_id=c tick=208 "
    pending="host seek release waiting run_id=c tick=208 phase=8 pending=true release_result=2"
    retired="seek checkpoint retired tick=208 application_idle=true"
    done=released+"surface_pending=false"
    assert validate_seek_release_retirement("\n".join((pending,retired,done)),released,208)["completed_release_after_retirement"]
    assert not validate_seek_release_retirement("\n".join((done,retired)),released,208)["completed_release_after_retirement"]
    assert validate_seek_release_retirement("\n".join((retired,done)),released,208,
        completed_release_required=True)["completed_release_after_retirement"]
    with pytest.raises(RuntimeError):
        validate_seek_release_retirement("\n".join((done,retired)),released,208,completed_release_required=True)
    for lines in ((pending,done,retired),(retired,pending,done),(pending,done),(pending,retired,retired,done)):
        with pytest.raises(RuntimeError):validate_seek_release_retirement("\n".join(lines),released,208)


def test_corrected_revision_completion_protocol_uses_native_commit():
    from replay_test import validate_corrected_revision_order
    publication="host seek publication observed run_id=c tick=205"
    revision="source revision activated owner=runtime policy=p"
    for completed,commit in [(False,"historical combat committed run_id=c"),
                              (True,"seek ownership state=Committing B_tick=210 target=210")]:
        validate_corrected_revision_order("\n".join((publication,revision,commit)),"host seek publication observed",completed)
        for lines in [(revision,publication,commit),(publication,commit,revision),(publication,revision),
                      (publication,revision,commit,commit),(publication,publication,revision,commit)]:
            with pytest.raises(RuntimeError):
                validate_corrected_revision_order("\n".join(lines),"host seek publication observed",completed)


def test_index_seek_boundary_split_requires_actual_rewind_and_completion(tmp_path):
    from tools.deterministic_qualification.replay_fidelity import indexed_seek_boundary_streams
    keys="round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs".split()
    common=" ".join(k+"=0" for k in keys)
    def row(ordinal,tick):return f"[ReplayQualification] boundary ordinal={ordinal} phase=actor_tail sample_version=2 frame={tick} {common}"
    lines=["boundaries started run_id=c native_frame=0",row(1,500),
           "index complete run_id=c entries=501 bytes=4096",
           "index closure run_id=c replay_tick=500 native_tick=500 callbacks_retained=true",
           "indexed seek begin run_id=c origin=500 target=208 full_index=true host_checkpoint=true callbacks=1 interval=500",
           "historical combat execution admitted run_id=c from_tick=500 to_tick=170 B_retained=true",
           row(2,170),row(3,328),"indexed seek continuation run_id=c first=209 last=328 ticks=120",
           "boundaries completed run_id=c observations=3 detached=true"]
    raw="\n".join(lines)
    rows,prefix,suffix,origin=indexed_seek_boundary_streams(raw,"c")
    assert len(rows)==3 and len(prefix)==1 and len(suffix)==2 and origin["origin"]==500
    for kind in ('later-middle', 'next-round'):
        inventory='\n'.join(f'index checkpoint owned tick={tick} retained={i} owned_bytes=4096'
            for i,tick in enumerate((170,250,350,450),1))
        managed=raw+'\n'+inventory+'\n'+ '\n'.join((
            'next-round index checkpoint selected origin=249 target=250 native_phase_tick=6 placement_owner=host',
            f'{kind} index checkpoint selected origin=349 target=350 native_phase_tick=6 placement_owner=host',
            'next-round index checkpoint selected origin=449 target=450 native_phase_tick=6 placement_owner=host',
            'indexed checkpoint selected run_id=c tick=170 extra=450 automatic=true'))
        assert indexed_seek_boundary_streams(managed,'c')[3]['checkpoint']==170
        for bad in (managed.replace('tick=350 retained=3','tick=250 retained=3'),
                    managed.replace('extra=450','extra=451')):
            with pytest.raises(RuntimeError):indexed_seek_boundary_streams(bad,'c')
    retained=raw.replace('replay_tick=500','replay_tick=494')+'\n'+ '\n'.join((
        'index retained range run_id=c source_tick=494 last_tick=500 tail_ticks=6 all_native_ticks_indexed=true',
        'index retained coverage source_tick=494 last_tick=500 tail_ticks=6 entries=501 all_native_ticks_indexed=true'))
    assert indexed_seek_boundary_streams(retained,'c')[3]['origin']==500
    for bad in (retained.replace('entries=501','entries=495'),retained.replace('tail_ticks=6','tail_ticks=5'),
                retained.replace('source_tick=494','source_tick=493'),retained.replace('last_tick=500','last_tick=499'),
                retained.replace('index retained range run_id=c','index retained range run_id=other'),
                retained.replace('all_native_ticks_indexed=true','all_native_ticks_indexed=false'),
                retained.split('\nindex retained coverage')[0],retained+'\nindex retained range run_id=c source_tick=494 last_tick=500 tail_ticks=6 all_native_ticks_indexed=true'):
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(bad,'c')
    later=raw.replace('origin=500','origin=1000').replace('native_tick=500','native_tick=1000').replace('replay_tick=500','replay_tick=1000').replace('entries=501','entries=1001').replace('from_tick=500','from_tick=1000').replace('frame=500','frame=1000').replace('target=208','target=300').replace('first=209 last=328 ticks=120','first=301 last=900 ticks=600').replace('frame=328','frame=900')
    selected=indexed_seek_boundary_streams(later,'c')[3]
    assert selected['target']==300 and selected['continuation_ticks']==600 and selected['last_tick']==900
    for bad in (later.replace('last=900','last=901'),later.replace('ticks=600','ticks=601'),later.replace('first=301','first=302')):
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(bad,'c')
    after=raw.replace("boundaries completed run_id=c observations=3 detached=true",
        row(4,329)+"\n"+"boundaries completed run_id=c observations=4 detached=true")
    all_rows,_,compared,details=indexed_seek_boundary_streams(after,"c")
    assert len(all_rows)==4 and len(compared)==2 and details["post_completion_callbacks"]==1
    from replay_test import executor_proof
    import hashlib
    accounting="\n".join([
        "executor started run_id=c native_frame=0",
        "index row tick=170 interval=170 publications=0",
        "historical execution rewind run_id=c ticks=330 intervals=330",
        "world executor completed run_id=c worlds=659 groups=659 tasks=659 manager_tasks=659 manager_yields=659 idle=true disabled=true",
        "engine executor completed run_id=c intervals=659 idle=true disabled=true",
        "application executor stopped intervals=659 idle=true disabled=true",
        "world arena completed run_id=c scopes=659 max_retained_bytes=64 probe_checks=2 empty=true",
        "executor boundaries run_id=c completed_intervals=659 zero_intervals=0 multi_intervals=0 repeat_requests=0 move_state_ticks=0 yielded_boundaries=659 yield_every_tick=true",
        "executor completed run_id=c ticks=329 intervals=329 publications=329 native_frame=500 disabled=true"])
    def proof(text):
        path=tmp_path/'accounting.log';path.write_text(text)
        return executor_proof({'run_id':'c','record_index':True,'index_seek':True,
            'raw_log':{'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}},True)
    assert proof(after+'\n'+accounting)['physical_native_frame']==329
    with pytest.raises(RuntimeError,match='traversal accounting'):
        proof(after+'\n'+accounting.replace('ticks=329 intervals=329','ticks=328 intervals=329'))
    from replay_test import verify_executor_native_counts
    evidence={'executor':{'ticks':3,'intervals':3,'publications':3,'physical_ticks':8,'physical_intervals':8,'physical_publications':8,
        'restore_trial_ticks':0,'zero_tick_intervals':0,'multi_tick_intervals':0,'move_state_ticks':0,'repeat_requests':0},
        'native_boundary_counts':{'actor_tail':8},'native_execution_coverage':{'tick_callbacks':8,'zero_tick_intervals':0,
        'multi_tick_intervals':0,'move_state_3_ticks':0,'input_cache_publications':8,'repeated_ticks_without_new_publication':0}}
    verify_executor_native_counts(evidence)
    for key in ('physical_ticks','physical_intervals','physical_publications'):
        evidence['executor'][key]-=1
        with pytest.raises(RuntimeError):verify_executor_native_counts(evidence)
        evidence['executor'][key]+=1
    early=raw.replace("indexed seek continuation run_id=c",row(4,329)+"\nindexed seek continuation run_id=c").replace("observations=3","observations=4")
    with pytest.raises(RuntimeError):indexed_seek_boundary_streams(early,"c")

    for bad in (raw.replace("callbacks=1","callbacks=2"),raw.replace("entries=501","entries=500"),
                raw.replace("to_tick=170","to_tick=171"),raw.replace("ticks=120","ticks=119"),
                raw.replace("frame=328","frame=329")):
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(bad,"c")


def test_indexed_seek_costs_distinguish_latency_and_resume():
    from replay_test import indexed_seek_costs
    text=("indexed seek ready run_id=c target=5750 elapsed_us=700000 owned_bytes=400000000 diagnostic_gpu_readbacks=false\nindexed seek resume rate run_id=c target=5750 ticks=120 elapsed_us=2000000\n"
          "indexed seek application cost run_id=c target=5750 preparation_us=100000 prefix_us=10000 engine_us=300000 tail_us=200000 frame_sync_us=10000 includes_observers=true\n"
          "indexed seek completion cost run_id=c target=5750 request_to_completed_us=1300000 validation_hold_us=500000 completion_us=100000 application_idle=true pending_task=false release_completed=true diagnostic_gpu_readbacks=false")
    result=indexed_seek_costs(text,"c",5750)
    assert result["seek_latency_result"]=="fail" and result["resume_rate_result"]=="pass"
    assert not result["practical_rollback_qualified"]
    assert not result["production_budget_qualified"]  # A ready-point count is not native peak proof.
    assert result["configured_production_budget"]
    assert result["peak_ownership_result"]=="not_measured"
    assert result["request_to_completed_us"]==1300000 and result["completed_work_excluding_validation_hold_us"]==800000
    for bad in (text.replace("400000000","1073741825"),text.replace("ticks=120","ticks=119"),text.replace("readbacks=false","readbacks=true"),text.replace("elapsed_us=700000","elapsed_us=0"),
                text.replace("release_completed=true","release_completed=false"),text.replace("validation_hold_us=500000","validation_hold_us=600000")):
        with pytest.raises(RuntimeError):indexed_seek_costs(bad,"c",5750)


@pytest.mark.parametrize("original,target",[(2504,5750),(300,300)])
def test_drained_cancellation_requires_fresh_fence_and_no_cpu_reopening(original,target):
    import replay_test
    rows=[("BRetained",original,0,0),("APublished",170,0,0),("ExecutionActive",170,0,0),
          ("CSettled",target,0,0),("CompletingTargetTails",target,0,0),("FailedRecoverable",target,0,0),
          ("RecoveryQuiescing",target,0,0),("Recovering",target,target,target-170),("Recovered",original,target,target-170)]
    initial=f"seek render drain completed tick={target} recovery=false gpu_complete=true render_settled=false"
    boundary=f"seek drained cancellation boundary tick={target} gpu_complete=true C_captured=true render_settled=false cpu_settled=0 B_retained=true"
    routed=f"seek drained recovery routed tick={target} gpu_complete=true render_settled=false cpu_settled=0 B_retained=true fresh_retirement_drain_required=true"
    fresh=f"seek render drain completed tick={target} recovery=true gpu_complete=true render_settled=false"
    lines=[]
    for state,current,tail,ticks in rows:
        if state=="FailedRecoverable": lines.extend([initial,boundary])
        lines.append(f"seek ownership state={state} B_tick={original} target={target} current_tick={current} completed_tail_tick={tail} "
                     f"B_retained={'false' if state=='Recovered' else 'true'} commit_decided=false executed_ticks={ticks} executed_intervals={ticks}")
        if state=="RecoveryQuiescing": lines.extend([routed,"seek C-only retirement owners=13 completed=13 B_retained=true native_render_drain_required=true",fresh])
    raw="\n".join(lines)
    assert replay_test.validate_seek_recovery_ownership(raw,170,original,target,target,True,True)["cancellation_boundary"]=="Render::Drained"
    for bad in [raw.replace(m,"") for m in (initial,boundary,routed,fresh)]+[
        raw.replace(fresh,initial),raw.replace("cpu_settled=0","cpu_settled=1"),
        raw+"\nseek CPU settlement reopened B_retained=true native_allocations_freed=0",
        raw.replace("commit_decided=false","commit_decided=true",1),
        raw.replace(fresh,"").replace(routed,fresh+"\n"+routed)]:
        with pytest.raises(RuntimeError): replay_test.validate_seek_recovery_ownership(bad,170,original,target,target,True,True)


@pytest.mark.parametrize("elapsed,expected", [(2000000, "pass"), (3000000, "fail")])
@pytest.mark.parametrize("target", [170,5750])
def test_indexed_seek_comparison_propagates_pacing_only(tmp_path, monkeypatch, elapsed, expected,target):
    import json
    import replay_test as module
    text=(f"indexed seek ready run_id=c target=5750 elapsed_us=700000 owned_bytes=400000000 diagnostic_gpu_readbacks=false\n"
          f"indexed seek resume rate run_id=c target=5750 ticks=120 elapsed_us={elapsed}\n"
          "indexed seek application cost run_id=c target=5750 preparation_us=100000 prefix_us=10000 engine_us=300000 tail_us=200000 frame_sync_us=10000 includes_observers=true\n"
          "indexed seek completion cost run_id=c target=5750 request_to_completed_us=1300000 validation_hold_us=500000 completion_us=100000 application_idle=true pending_task=false release_completed=true diagnostic_gpu_readbacks=false\n")
    text=text.replace("target=5750",f"target={target}")
    reports=[]
    for identity in ("n", "c"):
        raw=tmp_path / f"{identity}.log"; raw.write_text(text)
        report=tmp_path / f"{identity}.json"
        report.write_text(json.dumps({"run_id":identity,"raw_log":{"path":str(raw),"sha256":module.sha256_file(raw)},
                                     "index_seek_target":target,"index_seek_continuation":120}))
        reports.append(report)
    rows=[{"phase":"actor_tail","frame":target},{"phase":"input_cache_publication","frame":target,"inputs":"1,2"},{"phase":"actor_tail","frame":target+120}]
    if target!=170:
        rows=[{"phase":"actor_tail","frame":170},{"phase":"input_cache_publication","frame":170,"inputs":"1,2"}]+rows
    suffix=[dict(row) for row in rows[1:]]
    monkeypatch.setattr(module,"indexed_seek_boundary_streams",lambda *_:(None,None,suffix,{"checkpoint":170,"target":target,"last_tick":target+120,"continuation_ticks":120}))
    monkeypatch.setattr(module,"validate_boundary_capture",lambda *_:rows)
    monkeypatch.setattr(module,"compare_native_poses",lambda *_:{"result":"pass"})
    result=module.compare_indexed_seek_suffix(*reports)
    assert result["result"]==expected and result["resume_pacing_result"]==expected
    assert result["gameplay_result"]=="pass" and result["costs"]["seek_latency_result"]=="fail"
    assert not result["costs"]["practical_rollback_qualified"]
    assert result["complete_resimulation"]["callbacks_compared"]==len(suffix)

    if target!=170:
        # The target suffix still agrees, as it could after a native round reset.
        # A wrong consumed input on the approach must nevertheless reject.
        suffix[0]["inputs"]="0,2"
        with pytest.raises(RuntimeError,match="resimulation first callback mismatch"):
            module.compare_indexed_seek_suffix(*reports)
        suffix[0]["inputs"]="1,2"

    if target==170:
        suffix[0]={**suffix[0],"inputs":"0,2"}
        with pytest.raises(RuntimeError,match="first callback mismatch"):module.compare_indexed_seek_suffix(*reports)
        suffix[0]={"phase":"actor_tail","frame":170}
        with pytest.raises(RuntimeError,match="first native input publication"):module.compare_indexed_seek_suffix(*reports)


@pytest.mark.workflow
@pytest.mark.parametrize("failure",["pacing","hud","host_ownership"])
def test_indexed_seek_cli_fails_before_unnecessary_control(tmp_path, monkeypatch,failure):
    import json
    import replay_test as module
    monkeypatch.setattr(module, "OUTPUT", tmp_path)
    monkeypatch.setattr(module.sys, "argv", ["replay_test.py", "equivalence", "--full-match", "--index-seek"])
    monkeypatch.setattr(module, "ensure_build", lambda *_: {})
    monkeypatch.setattr(module, "sha256_file", lambda *_: "fixture")
    raw=tmp_path / "raw.log"; raw.write_text("fixture")
    (tmp_path / "baseline-a.json").write_text("{}")
    def args(*values):
        return SimpleNamespace(report=tmp_path / (values[2] + ".json"))
    def capture(options):
        report={"run_id":"fixture", "raw_log":{"path":str(raw)},
                "host_index_checkpoint":failure=="host_ownership"}
        options.report.write_text(json.dumps(report))
        return report
    monkeypatch.setattr(module, "control_args", args)
    monkeypatch.setattr(module, "capture", capture)
    monkeypatch.setattr(module, "require_bootstrap", lambda *_: None)
    monkeypatch.setattr(module, "validate_intro_skips", lambda *_: {})
    monkeypatch.setattr(module, "compare_passive_controls", lambda *_, **__: {"result":"pass"})
    monkeypatch.setattr(module, "validate_tick_index", lambda *_, **__: {"result":"pass"})
    def hud(*_):
        if failure=="hud":raise RuntimeError("same-process HUD mismatch")
        return {"result":"pass"}
    monkeypatch.setattr(module,"validate_indexed_same_process_hud",hud)
    monkeypatch.setattr(module, "compare_indexed_seek_suffix", lambda *_: {
        "result":"fail", "gameplay_result":"pass", "resume_pacing_result":"fail"})
    assert module.main()==1
    report=json.loads((tmp_path / "equivalence-full-index-checkpoint-stage.json").read_text())
    assert report["result"]=="fail"
    if failure!="pacing":
        expected="same-process HUD mismatch" if failure=="hud" else "host index checkpoint lacks ordered"
        assert expected in report["failure"] and "indexed_seek" not in report
        assert not (tmp_path/"equivalence-full-index-checkpoint-native.json").exists()
        assert report["native_control_report"] is None
    else:
        assert report["indexed_seek"]["gameplay_result"]=="pass" and "below 58" in report["failure"]
        assert report["native_control_report"]==str(tmp_path/"equivalence-full-index-checkpoint-native.json")


def test_changed_input_coverage_distinguishes_resimulation_from_future_continuation():
    from replay_test import changed_input_target_coverage
    assert changed_input_target_coverage(208,[245,246],[252,253])["result"]=="not_exercised"
    assert changed_input_target_coverage(245,[245],[252])["result"]=="not_exercised"
    assert changed_input_target_coverage(300,[245],[301])["result"]=="not_exercised"
    result=changed_input_target_coverage(300,[245,300,301],[252,300,301])
    assert result["result"]=="pass"
    assert result["changed_publication_ticks"]==[245] and result["changed_gameplay_ticks"]==[252]


@pytest.mark.workflow
@pytest.mark.parametrize("removed",[None,"--corrected-inputs","--source-revision","--host-seek"])
@pytest.mark.parametrize("original",["300","2504"])
def test_guard_resimulation_cli_requires_bounded_correction_protocol(tmp_path,monkeypatch,removed,original):
    import replay_test as module
    args=["replay_test.py","combat-restore","--host-seek","--combat-exact-advance",
          "--combat-case","commit","--combat-anchor-tick","170","--combat-advanced-tick",original,
          "--host-seek-target","300","--corrected-inputs","--source-revision"]
    if removed:args.remove(removed)
    monkeypatch.setattr(module.sys,"argv",args)
    monkeypatch.setattr(module,"OUTPUT",tmp_path)
    entered=[]
    def boundary(*_):
        entered.append(True)
        raise RuntimeError("fixture stopped before build or live launch")
    monkeypatch.setattr(module,"ensure_build",boundary)
    if removed:
        with pytest.raises(SystemExit):module.main()
        assert not entered
    else:
        assert module.main()==1 and entered==[True]


def test_same_origin_guard_lower_capture_admission():
    from tools.deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=300,host_seek_target=300,host_seek=True,
                corrected_inputs=True,source_revision=True,source_revision_profile="guard201",historical_cancel="",host_seek_repeat=False)
    assert historical_capture_interval_supported(report)
    for field,value in [("host_seek",False),("corrected_inputs",False),("source_revision",False),
                        ("source_revision_profile","historical"),("historical_cancel","after"),("host_seek_repeat",True),
                        ("host_seek_target",299)]:
        assert not historical_capture_interval_supported(dict(report,**{field:value}))


@pytest.mark.parametrize("cancel",["before","after"])
def test_private_hud_recovery_lower_admission(cancel):
    from tools.deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=300,host_seek_target=208,
                host_seek=True,historical_cancel=cancel,checkpoint_pair=False)
    assert historical_capture_interval_supported(report)
    for change in [dict(host_seek=False),dict(historical_cancel=""),dict(host_seek_target=209),
                   dict(corrected_inputs=True),dict(seek_advance_failure=True),dict(host_seek_repeat=True)]:
        assert not historical_capture_interval_supported(dict(report,**change))


@pytest.mark.workflow
@pytest.mark.parametrize("cancel",["before","after"])
def test_private_hud_recovery_cli_reaches_build(tmp_path,monkeypatch,cancel):
    import replay_test as module
    monkeypatch.setattr(module.sys,"argv",["replay_test.py","combat-restore","--host-seek",
        "--combat-case",cancel,"--combat-anchor-tick","170","--combat-advanced-tick","300"])
    monkeypatch.setattr(module,"OUTPUT",tmp_path)
    entered=[]
    def boundary(*_):
        entered.append(True);raise RuntimeError("fixture stopped before build or live launch")
    monkeypatch.setattr(module,"ensure_build",boundary)
    assert module.main()==1 and entered==[True]


def test_private_hud_continuation_requires_every_native_observation():
    from replay_test import compare_private_hud_continuation
    text="\n".join(f"HUD state tick={tick} p1=0 p2=0 combo1=0 combo2=0 timer=0 damage=1 types=1 pool=10 active=0 type_pool=10 type_active={int(tick<310)} type_players=1234 read_only=true" for tick in range(301,421))
    assert compare_private_hud_continuation(text,text)["ticks"]==120
    for invalid in [text.replace("tick=310 ","tick=311 "),text.replace("tick=420 ","tick=421 "),
                    text.replace("type_players=1234","type_players=5678",1),text.replace("read_only=true","read_only=false",1)]:
        with pytest.raises(RuntimeError):compare_private_hud_continuation(text,invalid)


def test_private_hud_drained_recovery_admission():
    from deterministic_qualification.replay_fidelity import private_hud_drained_case
    from deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=300,host_seek_target=300,
        host_seek=True,historical_cancel="after",seek_advance_failure=True,seek_settlement_failure=True,
        seek_drained_cancel=True,checkpoint_pair=False)
    assert private_hud_drained_case(report) and historical_capture_interval_supported(report)
    for key in ("seek_advance_failure","seek_settlement_failure","seek_drained_cancel","host_seek"):
        invalid=dict(report,**{key:False});assert not private_hud_drained_case(invalid)
        assert not historical_capture_interval_supported(invalid)
    for change in [dict(historical_cancel="before"),dict(host_seek_target=301),dict(corrected_inputs=True),
                   dict(source_revision=True),dict(host_seek_repeat=True),dict(seek_observer_failure=True)]:
        assert not private_hud_drained_case(dict(report,**change))


@pytest.mark.workflow
def test_private_hud_drained_recovery_cli_reaches_build(tmp_path,monkeypatch):
    import replay_test as module
    monkeypatch.setattr(module.sys,"argv",["replay_test.py","combat-restore","--host-seek",
        "--combat-case","after","--combat-anchor-tick","170","--combat-advanced-tick","300",
        "--host-seek-target","300","--seek-drained-cancel"])
    monkeypatch.setattr(module,"OUTPUT",tmp_path);entered=[]
    def boundary(*_):
        entered.append(True);raise RuntimeError("fixture stopped before build or live launch")
    monkeypatch.setattr(module,"ensure_build",boundary)
    assert module.main()==1 and entered==[True]


def test_indexed_control_events_survive_bursts_and_partial_lines(tmp_path):
    from deterministic_qualification.replay_controls import IndexedControlEvents
    path=tmp_path / "run.log";reader=IndexedControlEvents()
    resumed=b"indexed seek resumed run_id=test tick=208\n"
    path.write_bytes(b"startup "*16+b"\n"+resumed+b"observation\n"*20000+b"replay button name=pa")
    assert reader.read(path).encode()==resumed.rstrip()
    with path.open("ab") as stream:stream.write(b"use hwnd=10 screen_x=1 screen_y=2 tick=209\n")
    events=reader.read(path)
    assert resumed.decode().strip() in events and "name=pause" in events
    assert reader.read(path)==events and reader.witness()["events"]==2
    assert reader.witness()["retained_event_bytes"]<200
    path.write_bytes(b"replaced "*16)
    with pytest.raises(RuntimeError):reader.read(path)


@pytest.mark.native_contract
def test_native_interactive_request_does_not_insert_options(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "tools/replay_qualification_mod/ReplayQualificationMod.cpp").read_text()
    start=source.index('        if (fields.contains("probe_interactive_controls"))')
    end=source.index('        if (fields.contains("probe_application_pause"))',start)
    (tmp_path / "request.inl").write_text(source[start:end])
    fixture=tmp_path / "request.cpp"
    fixture.write_text('''#include <string>
#include <unordered_map>
using Fields=std::unordered_map<std::string,std::string>;
bool admit(Fields& fields,bool interior){
 struct {bool probe_interior_pause,probe_interactive_controls{};} output{interior};
 #include "request.inl"
 return output.probe_interactive_controls;
}
int main(){
 for(int mode=0;mode<4;++mode){
  Fields fields{{"probe_interactive_controls","true"}};
  if(mode==1)fields["index_seek"]="true";
  if(mode==2)fields["seek_settlement_failure"]="true";
  auto original=fields;
  if(admit(fields,mode==3)!=(mode!=0) || fields!=original)return 1;
 }
 Fields fields{{"probe_interactive_controls","false"},{"index_seek","true"}};
 auto original=fields;if(admit(fields,false) || fields!=original)return 2;
}
''')
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:request.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "request.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_indexed_control_progress_timeout(monkeypatch):
    from deterministic_qualification import replay_controls as controls
    now=[0.0]
    monkeypatch.setattr(controls.time,'monotonic',lambda:now[0])
    report=dict(run_id='r',indexed_ui=dict(focus=dict(pid=7,hwnd=9)))
    text='indexed seek resumed run_id=r target=208'
    controls.drive_indexed_controls(report,7,text,lambda *_:None,lambda *_:None)
    now[0]=10.01
    with pytest.raises(RuntimeError,match='no control progress'):
        controls.drive_indexed_controls(report,7,text,lambda *_:None,lambda *_:None)
    report['indexed_ui']['pause_input']={}
    controls.drive_indexed_controls(report,7,text,lambda *_:None,lambda *_:None)
    now[0]=20.02
    with pytest.raises(RuntimeError,match='no control progress'):
        controls.drive_indexed_controls(report,7,text,lambda *_:None,lambda *_:None)


def test_indexed_control_image_before_driver_admission(tmp_path,monkeypatch):
    import ctypes
    from PIL import Image,ImageGrab
    from deterministic_qualification.replay_controls import capture_indexed_control_hold
    class Function:
        def __init__(self,call):self.call=call
        def __call__(self,*args):return self.call(*args)
    def owner(hwnd,out):
        assert hwnd==45
        out._obj.value=7
        return 1
    def rectangle(hwnd,out):
        assert hwnd==45
        out._obj.left=out._obj.top=0
        out._obj.right=320;out._obj.bottom=200
        return 1
    api=SimpleNamespace(GetWindowThreadProcessId=Function(owner),GetWindowRect=Function(rectangle))
    monkeypatch.setattr(ctypes,'WinDLL',lambda *_,**__:api)
    grabbed=[]
    def grab(window,size):
        grabbed.append(window)
        return Image.new('RGB',(320,200))
    monkeypatch.setattr('deterministic_qualification.replay_controls.capture_owned_window',grab)
    report=dict(run_id='r',indexed_ui=dict(focus=dict(pid=7,hwnd=9)))
    text='playback controls held run_id=r tick=231 resume_baseline=0\nprivate replay output displayed tick=231 hwnd=45 native_target_untouched=true'
    path=tmp_path/'run.json'
    capture_indexed_control_hold(report,path,7,text)
    assert grabbed==[45] and 'tick' not in report['indexed_ui']
    assert report.get('desktop_capture_timing_perturbed') is True
    assert report['indexed_ui']['images']['held']['tick']==231
    assert Path(report['indexed_ui']['images']['held']['path']).is_file()
    capture_indexed_control_hold(report,path,7,text)
    assert grabbed==[45]
    report['indexed_ui']['submit_input']={}
    with pytest.raises(RuntimeError,match='lost its replay-output owner'):
        capture_indexed_control_hold(report,path,8,text)
    assert grabbed==[45]


def test_indexed_control_driver_and_evidence():
    from deterministic_qualification.replay_controls import drive_indexed_controls,validate_indexed_controls
    import copy
    report={"run_id":"test","index_seek_target":208,"index_seek_continuation":240}
    clicks=[]
    def click(pid,hwnd,x,y):
        clicks.append((x,y))
        return {"method":"Windows SendInput","button_released":True,"pid":pid,"hwnd":hwnd}
    focus=lambda pid:{"pid":pid,"hwnd":10}
    raw="[GameImGui] DX11 overlay initialised \n"
    drive_indexed_controls(report,1,raw,click,focus)
    raw+="indexed seek resumed run_id=test tick=208 target_tails_complete=true\nreplay button name=pause hwnd=10 screen_x=1 screen_y=2 tick=210\n"
    drive_indexed_controls(report,1,raw,click,focus)
    raw+="playback controls held run_id=test tick=220 resume_baseline=0\n"
    names=["number_open","number_clear","2","0","number_submit"]
    raw+=''.join(f"replay button name={name} hwnd=10 screen_x={i+3} screen_y=2 tick=220\n" for i,name in enumerate(names))
    for _ in range(6):drive_indexed_controls(report,1,raw,click,focus)
    assert len(clicks)==7
    raw+="numeric replay seek submitted target=220\nindexed seek already held tick=220\nreplay resume control hwnd=10 screen_x=8 screen_y=2\n"
    drive_indexed_controls(report,1,raw,click,focus)
    drive_indexed_controls(report,1,raw,click,focus)
    assert len(clicks)==8
    raw+="playback controls resumed run_id=test tick=220 held_us=2000000 frames=120 unchanged=true application_complete=true\nindexed seek resume rate run_id=test target=208 ticks=120 elapsed_us=2000000 origin=220\n"
    assert validate_indexed_controls(report,raw)["independent_continuation_available"]==228
    for bad in (raw.replace("origin=220","origin=208"),raw.replace("held_us=2000000","held_us=1"),raw.replace("indexed seek already held tick=220", "")):
        with pytest.raises(RuntimeError):validate_indexed_controls(report,bad)
    bad_report=copy.deepcopy(report);bad_report["indexed_ui"]["digits"][0]["input"]["pid"]=2
    with pytest.raises(RuntimeError):validate_indexed_controls(bad_report,raw)
    report['indexed_control_protocol']='numeric_initial_seek_v2'
    with pytest.raises(RuntimeError):validate_indexed_controls(report,raw)
    receipt={"method":"Windows SendInput","button_released":True,"pid":1,"hwnd":10}
    report['indexed_ui']['entry']={'origin':11148,'target':208,'open_input':receipt,'clear_input':receipt,'submit_input':receipt,
        'digits':[{'digit':digit,'input':receipt} for digit in '208']}
    entry='indexed numeric entry waiting run_id=test origin=11148 target=208\nnumeric replay seek submitted target=208\nindexed numeric entry accepted run_id=test origin=11148 target=208 production_seek=true\n'
    combined=entry+raw
    assert validate_indexed_controls(report,combined)['initial_different_target_numeric_seek']
    private_report=copy.deepcopy(report)
    for row in [private_report['indexed_ui']['entry'][k] for k in ('open_input','clear_input','submit_input')]+[x['input'] for x in private_report['indexed_ui']['entry']['digits']]:
        row.update(hwnd=11,window_class='HorseModReplayOutputUi',foreground_verified=True)
    private_raw=combined+'private replay output displayed tick=11148 hwnd=11 native_target_untouched=true\n'
    assert validate_indexed_controls(private_report,private_raw)['initial_different_target_numeric_seek']
    for field,value in [('pid',2),('window_class','Foreign'),('foreground_verified',False),('hwnd',12)]:
        bad=copy.deepcopy(private_report);bad['indexed_ui']['entry']['open_input'][field]=value
        with pytest.raises(RuntimeError):validate_indexed_controls(bad,private_raw)
    for bad in (combined.replace('production_seek=true','production_seek=false'),combined.replace('numeric replay seek submitted target=208',''),combined.replace('origin=11148','origin=220')):
        with pytest.raises(RuntimeError):validate_indexed_controls(report,bad)


@pytest.mark.native_contract
def test_replay_numeric_tick_draft_bounds(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=tmp_path / "tick-entry.cpp"
    source.write_text('''#include "GameImGui/ReplayTickEntry.hpp"
int main(){
 Horse::GameImGui::ReplayTickEntry entry;
 if(entry.valid(999))return 1;
 for(unsigned digit:{2u,0u,8u})if(!entry.append(digit))return 2;
 if(entry.value()!=208 || !entry.valid(208) || entry.valid(207))return 3;
 entry.erase();if(entry.value()!=20)return 4;
 entry.clear();if(!entry.empty() || entry.valid(208))return 5;
 if(!entry.append(0) || !entry.valid(0))return 6;
 entry.reset(UINT64_MAX);if(entry.append(0) || entry.append(10) || entry.value()!=UINT64_MAX)return 7;
 entry.erase();if(!entry.append(5) || entry.value()!=UINT64_MAX)return 8;
 entry.reset(9);entry.erase();if(!entry.empty())return 9;
}
'''.replace('#include "GameImGui/ReplayTickEntry.hpp"','#include <initializer_list>\n#include "GameImGui/ReplayTickEntry.hpp"'))
    batch=tmp_path / "compile.cmd"
    includes=module.ROOT / "HorseMod/horselib"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{includes}" "{source}" /Fe:tick-entry.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "tick-entry.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_flat_object_iteration_live_header(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "RE-UE4SS/deps/first/Unreal/src/UObjectGlobals.cpp").read_text()
    start=source.index("    using ForEachUObjectCallback =")
    end=source.index("    static auto ForEachUObject_Chunked(",start)
    (tmp_path / "replay_flat_object_iterator.inl").write_text(source[start:end])
    fixture=module.ROOT / "tools/replay_object_array_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:flat-visit.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "flat-visit.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert "live growth, relocation, shrink, exclusions and early break passed" in checked.stdout


@pytest.mark.native_contract
def test_production_object_visitor_selection_and_rejection(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    # Substitute only framework declarations; compile the full production
    # visitor header, not a copied predicate or alternate enumeration policy.
    for name in ("Unreal/UObjectGlobals.hpp", "Unreal/UObject.hpp", "Unreal/CoreUObject/UObject/Class.hpp"):
        header=tmp_path / name
        header.parent.mkdir(parents=True,exist_ok=True)
        header.write_text("#pragma once\n")
    fixture=module.ROOT / "tools/replay_object_visit_selftest.cpp"
    includes=module.ROOT / "HorseMod/horselib"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{includes}" "{fixture}" /Fe:object-visit.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "object-visit.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert "selection, exclusions and early rejection passed" in checked.stdout


@pytest.mark.native_contract
def test_production_trace_capture_accounting_and_release(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp").read_text()
    add=source[source.index("    bool Add("):source.index("    bool ArrayImage(")]
    accounting=source[source.index("    Status ReleaseCapture()"):source.index("    Status Capture(std::uintptr_t")]
    scene=source[source.index("    bool SceneTransform("):source.index("    bool CaptureDynamic(")]
    write=source[source.index("    static bool Write("):source.index("    bool Add(")]
    begin=source.index("            const auto component_id=Identify(component);")
    root=source[begin:source.index("            // One-shot completion",begin)]
    root="Status CaptureRoot(Object* component,void* world,std::uintptr_t base,std::vector<void*>& owners) {for(auto* once:{component}) {\n"+root+"}return Status::success();}\n"
    (tmp_path / "replay_trace_capture_methods.inl").write_text(add+accounting+scene+write+root,encoding="utf-8")
    fixture=module.ROOT / "tools/replay_trace_capture_accounting_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:trace-capture.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "trace-capture.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert "capacity rejection and retained-owner release passed" in checked.stdout


@pytest.mark.native_contract
def test_trace_material_publication_rejects_unsupported_mid_vector_state(tmp_path):
    """G1 fail-closed admission only; no native material settlement/undo proof.

    The actual Capture and Prepare methods must decide admission. Prepare is
    called by production PrepareStorage and again by Prepared::Publish through
    PrepareInitial for this unchanged-owner case. The fixture does not grant a
    writer guard, proxy lease or completion predicate. B is captured before A
    admission; raw B comparison bytes are never imported into production.

    Stop at the first unsupported boundary. Native vector/conditional-refresh
    ordering, Start-created providers and safe B publication/retirement still
    require native ownership/completion proof before a delayed-task test can
    faithfully exercise them. This remains a required regression, without xfail.
    """
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS

    root=module.ROOT/'HorseMod/horselib/deterministic'
    source=(root/'Sc6ReplayTraceState.hpp').read_text(encoding='utf-8')
    declarations=source[source.index('    using Object='):source.index('    bool OwnsExpired(')]
    memory=source[source.index('    Id Identify('):source.index('public:\n    struct RootConsumer')]
    capture=source[source.index('    Status ReleaseCapture()'):source.index('    // Called only by the enclosing ordered render command')]
    start=source.index('    Status ValidateBindings() const {')
    publication=source[start:source.index('\n};',start)]
    (tmp_path/'trace_material_declarations.inl').write_text(declarations,encoding='utf-8')
    (tmp_path/'trace_material_methods.inl').write_text(memory+capture+publication,encoding='utf-8')
    visit=(root/'Sc6ReplayObjectVisit.hpp').read_text(encoding='utf-8')
    (tmp_path/'trace_material_visit_layout.inl').write_text(
        visit[visit.index('struct ReplayObjectVisitClassEntry'):visit.index('// Same class/superclass')],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_capture_accounting_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /DREPLAY_TRACE_MATERIAL_ADMISSION /I. /I"{root}" "{fixture}" /Fe:trace-material.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    child=subprocess.run([str(tmp_path/'trace-material.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=f'trace material admission child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert 'material-free production capture/Prepare/Install control passed' in child.stdout,detail
    assert child.returncode==0,detail
    assert 'unsupported trace MID rejected before A publication; B unchanged' in child.stdout,detail
    assert 'trace material fallback, null slots and malformed storage controls passed' in child.stdout,detail


@pytest.mark.native_contract
def test_production_index_checkpoint_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    header=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.hpp").read_text()
    declarations=header[header.index("    enum class IndexCheckpointPhase"):header.index("    // Adopt an existing completed-application hold")]
    (tmp_path / "replay_index_checkpoint_declarations.inl").write_text(declarations)
    selector=header[header.index("    enum class IndexCheckpointSelection"):header.index("    std::int32_t index_checkpoint_round_")]
    (tmp_path / "replay_index_checkpoint_selection.inl").write_text(selector)
    interior=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.Interior.inl").read_text()
    pause_start=interior.index("void Sc6ReplayHost::TrySuspendApplication()")
    pause_end=interior.index("    if(tick_advance_.phase==TickAdvancePhase::Settling",pause_start)
    (tmp_path / "replay_playback_pause_selection.inl").write_text(interior[pause_start:pause_end]+"}\n")
    monitor=interior[interior.index("bool Sc6ReplayHost::SetPauseMonitor"):interior.index("Sc6ReplayHost::TickAdvanceWitness Sc6ReplayHost::ReadTickAdvance")]
    (tmp_path / "replay_index_pause_monitor.inl").write_text(monitor)
    index=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.Index.inl").read_text()
    controls=index[index.index("void Sc6ReplayHost::ServiceIndexControls()"):index.index("void Sc6ReplayHost::FinishIndexAtApplicationBoundary()")]
    (tmp_path / "replay_index_controls.inl").write_text(controls)
    fixture=module.ROOT / "tools/replay_index_checkpoint_selftest.cpp"
    includes=module.ROOT / "HorseMod/horselib"
    generated=module.BUILD / "HorseMod/generated"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{includes}" /I"{generated}" "{fixture}" /Fe:index-checkpoint.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "index-checkpoint.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert "cancellation and retirement passed" in checked.stdout


@pytest.mark.parametrize("cancelled",[False,True])
@pytest.mark.parametrize("missing",[None,0,1,2,3,4])
def test_host_index_checkpoint_requires_completed_ownership(cancelled,missing):
    from replay_test import validate_host_index_checkpoint
    rows=["host index checkpoint adopted run_id=c tick=170 capture_retention_and_retirement=host observer=read_only",
          "index checkpoint owned tick=170 retained=1 owned_bytes=123",
          "index checkpoint retained run_id=c tick=170 entries=171 elapsed_us=99",
          "index held cancellation completed run_id=c tick=170 entries=0 bytes=0" if cancelled else
          "index checkpoint capture retired run_id=c tick=170 immutable_owner_retained=true",
          f"host index checkpoint completed run_id=c tick=170 pending=false cancelled={str(cancelled).lower()}"]
    if missing is not None:rows.pop(missing)
    if cancelled:rows.insert(min(3,len(rows)),"index held cancellation queued run_id=c tick=170 entries=171 bytes=123 complete=false cleanup_owner=host")
    raw="\n".join(rows)
    if missing is None:assert validate_host_index_checkpoint(raw,"c",cancelled)["result"]=="pass"
    else:
        with pytest.raises(RuntimeError):validate_host_index_checkpoint(raw,"c",cancelled)
    if missing is None:
        with pytest.raises(RuntimeError):validate_host_index_checkpoint(raw,"c",cancelled,True)
        armed="host index checkpoint armed run_id=c origin=169 target=170\n"
        assert validate_host_index_checkpoint(armed+raw,"c",cancelled,True)["boundary_arming_owner"]=="host"
        with pytest.raises(RuntimeError):validate_host_index_checkpoint(raw+"\n"+armed,"c",cancelled,True)


@pytest.mark.parametrize("host_owned", [False, True])
def test_retained_index_proof_requires_advertised_host_ownership(tmp_path, monkeypatch, host_owned):
    import json
    import replay_test as module
    monkeypatch.setattr(module, "OUTPUT", tmp_path)
    raw = tmp_path / "capture.log"
    raw.write_text("no host completion receipt")
    for label in ("native", "candidate"):
        (tmp_path / f"index-owned-checkpoint-{label}.json").write_text(json.dumps({
            "record_index": True, "full_match": True, "include_setup": True,
            "host_index_checkpoint": host_owned and label == "candidate",
            "run_id": label, "raw_log": {"path": str(raw)}}))
    def compare(*args, **kwargs):
        raise RuntimeError("independent comparison reached")
    monkeypatch.setattr(module, "compare_passive_controls", compare)
    expected = "host index checkpoint lacks ordered" if host_owned else "independent comparison reached"
    with pytest.raises(RuntimeError, match=expected):
        module.qualify_retained_index(True)


@pytest.fixture(scope='session')
def trace_storage_executable(tmp_path_factory):
    """Compile shared production fragments once; each test runs a fresh process."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    tmp_path = tmp_path_factory.mktemp('trace-storage-build')
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayTraceRetirement.inl").read_text()
    journal=source[source.index("    struct Retirement {"):source.index("    Status PrepareAddedRetirement")]
    method=source[source.index("    bool RetireChildrenNative"):source.index("\npublic:",source.index("    bool RetireChildrenNative"))]
    recovery=source[source.index("    Status RetireAddedForUndo"):source.index("\nprivate:",source.index("    Status RetireAddedForUndo"))]
    ownership=source[source.index("    static bool StorageOverlaps"):source.index("    bool OwnsNativeChild")]
    historical=source[source.index("    bool HasAddedStates"):source.index("    Status PrepareExecution")]
    (tmp_path / "replay_trace_retirement_method.inl").write_text(ownership+journal+method+recovery+historical)
    root=module.ROOT / "HorseMod/horselib/deterministic"
    state=(root / "Sc6ReplayTraceState.hpp").read_text(encoding="utf-8")
    retirement=(root / "Sc6ReplayTraceRetirement.inl").read_text(encoding="utf-8")
    image=state[state.index("    static bool Equal"):state.index("    bool Add(")]
    image+=state[state.index("    static bool MaterialRangeReadable("):state.index("    Status Prepare(const Sc6ReplayTraceState&")]
    image+=state[state.index("    Status Prepare(const Sc6ReplayTraceState&"):state.index("    Status ValidateValues()",state.index("    Status Prepare(const Sc6ReplayTraceState&"))]
    image+=state[state.index("    Status Install() const"):state.index("\n};",state.index("    Status Install() const"))]
    image+=retirement[retirement.index("    bool OwnsNativeChild"):retirement.index("    static bool EmptyEvent")]
    image+=retirement[retirement.index("    Status PrepareExecution"): ]
    fresh=(root / "Sc6ReplayTraceFreshChild.inl").read_text(encoding="utf-8")
    image+=fresh[fresh.index("Status ValidateFreshChildExecution("):fresh.index("Status FreshChildMeshBinding(")]
    image+=fresh[fresh.index("Status ReleaseExecutedFreshChildOwnership("):fresh.index("// The host calls this only after the child's retirement")]
    (tmp_path / "trace_storage_image_methods.inl").write_text(image,encoding="utf-8")
    host=(root / "Sc6ReplayHost.ParticleOwners.inl").read_text(encoding="utf-8")
    (tmp_path / "trace_storage_host_release.inl").write_text(host[host.index("Status Sc6ReplayHost::ReleaseFreshParticleOwnership("):],encoding="utf-8")
    release=(root / "Sc6ReplayHost.Restore.inl").read_text(encoding="utf-8")
    # This fixture covers the native-owner half of release. The enclosing
    # material veto is exercised with real guard wrappers in the VFX fixture.
    (tmp_path / "trace_storage_host_can_release.inl").write_text(release[release.index("bool Sc6ReplayHost::HistoricalNativeOwnersReleased("):release.index("bool Sc6ReplayHost::RestoreOperation(")],encoding="utf-8")
    source=(root / "Sc6ReplayTraceStorage.inl").read_text(encoding="utf-8")
    (tmp_path / "trace_storage_prepared.inl").write_text(source[source.index("class Sc6ReplayTraceState::Prepared final"):source.rfind("\n}")],encoding="utf-8")
    scheduler=(root / "Sc6ReplaySchedulerState.Storage.cpp").read_text(encoding="utf-8")
    (tmp_path / "trace_storage_scheduler_methods.inl").write_text(scheduler[scheduler.index("Status Sc6ReplaySchedulerState::PreparedRestore::ValidatePrivateOwners"):scheduler.index("Status Sc6ReplaySchedulerState::UndoBacking")],encoding="utf-8")
    fixture=module.ROOT / "tools/replay_trace_storage_selftest.cpp"
    batch=tmp_path / "compile-storage.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{root}" "{fixture}" /Fe:trace-storage.exe /Fo:storage.obj\n',encoding="utf-8")
    compiled=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert compiled.returncode==0,compiled.stdout+compiled.stderr
    return tmp_path / 'trace-storage.exe'


@pytest.mark.native_contract
def test_production_trace_retirement_method_fault_boundaries(tmp_path, trace_storage_executable):
    import subprocess
    checked = subprocess.run([str(trace_storage_executable), 'retirement'], cwd=tmp_path,
                             capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert '17 fault boundaries' in checked.stdout



@pytest.mark.native_contract
def test_production_trace_private_storage_publication_undo(tmp_path, trace_storage_executable):
    import subprocess
    checked = subprocess.run([str(trace_storage_executable)], cwd=tmp_path,
                             capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert 'complete held B undo' in checked.stdout



@pytest.mark.parametrize("selected", [170, 5750])
@pytest.mark.parametrize("offset", [0, 60])
def test_additional_index_checkpoint_selection_retirement_and_budget(selected,offset):
    from replay_test import validate_index_checkpoint_retention
    extra=5750+offset
    if selected!=170:selected=extra
    rows=[]
    for ordinal,tick in enumerate((170,extra),1):
        if tick==extra:
            rows.extend(f"indexed checkpoint deferred run_id=c requested=5750 tick={at} reason=active_HUD_players captured=false" for at in range(5750,extra,30))
        rows.extend((f"index checkpoint owned tick={tick} retained={ordinal} owned_bytes=440000000",
            f"index checkpoint retained run_id=c tick={tick} entries={tick+1} elapsed_us=99000 owned_bytes=440000000 callbacks_unchanged=true application_idle=true",
            f"index checkpoint capture retired run_id=c tick={tick} immutable_owner_retained=true"))
    rows.extend(("index complete run_id=c",f"indexed checkpoint selected run_id=c tick={selected} extra={extra} automatic=true"))
    if selected==170:rows.append(f"indexed checkpoint fallback run_id=c rejected={extra} selected=170 reason=retained_owner_lifetime")
    for tick in (170,extra):rows.append(f"index checkpoint release witness tick={tick} current_tick=11120 vfx_owners_live=false target_shape_supported=true owned_bytes=440000000")
    rows.extend(("seek checkpoint retired tick=11120 application_idle=true","index released run_id=c bytes=0 hooks_removed=true"))
    raw="\n".join(rows)
    result=validate_index_checkpoint_retention(raw,"c",5750)
    assert result["additional_checkpoint"]["selected_tick"]==selected
    assert result["additional_checkpoint"]["automatic_fallback"]==(selected==170)
    for broken in (raw.replace("retained=2","retained=1"),raw.replace("440000000","1073741825"),
                   raw.replace(f"tick={extra} immutable_owner_retained=true",f"tick={extra} immutable_owner_retained=false"),
                   raw.replace(f"extra={extra}","extra=5800"),raw.replace(f"tick={extra} current_tick=11120",f"tick={extra} current_tick=5000")):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(broken,"c",5750)
    if selected==170:
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(raw.replace("reason=retained_owner_lifetime","reason=unknown"),"c",5750)

    if not offset:
        # Managed placement follows the native round phase, not the old
        # observer's thirty-tick retry grid or requested lower bound.
        managed='automatic replay session indexed native_tick=0\n'+raw
        managed=managed.replace('tick=5750','tick=5757').replace('entries=5751','entries=5758').replace('extra=5750','extra=5757').replace('rejected=5750','rejected=5757')
        placement=('next-round index checkpoint selected origin=5756 target=5757 native_phase_tick=6 placement_owner=host\n'
            'host index checkpoint armed run_id=c origin=5756 target=5757\n'
            'host index checkpoint adopted run_id=c tick=5757 capture_retention_and_retirement=host observer=read_only\n')
        managed=managed.replace('index checkpoint owned tick=5757',placement+'index checkpoint owned tick=5757')
        retired='index checkpoint capture retired run_id=c tick=5757 immutable_owner_retained=true'
        managed=managed.replace(retired,retired+'\nhost index checkpoint completed run_id=c tick=5757 pending=false cancelled=false')
        proof=validate_index_checkpoint_retention(managed,'c',5750)['additional_checkpoint']
        assert proof['checkpoint_tick']==5757 and proof['placement']['completed_resume']
        for bad in (managed.replace(placement,''),managed.replace('native_phase_tick=6','native_phase_tick=0'),
                    managed.replace('pending=false cancelled=false','pending=true cancelled=false')):
            with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',5750)

    if offset:
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(raw.replace("tick=5780 reason=active_HUD_players","tick=5781 reason=active_HUD_players"),"c",5750)

        rejected="indexed checkpoint capture rejected run_id=c tick=5780 code=5 resources_retired=true operation_idle=true"
        skipped="indexed checkpoint deferred run_id=c requested=5750 tick=5780 reason=unsupported_capture captured=true"
        recovered=raw.replace("indexed checkpoint deferred run_id=c requested=5750 tick=5780 reason=active_HUD_players captured=false",rejected+"\n"+skipped)
        assert validate_index_checkpoint_retention(recovered,"c",5750)["additional_checkpoint"]["unsupported_boundaries"][1]["capture_attempted"]
        for broken in (recovered.replace("resources_retired=true","resources_retired=false"),recovered.replace("code=5 resources","code=9 resources"),
                       recovered.replace(rejected+"\n"+skipped,skipped+"\n"+rejected)):
            with pytest.raises(RuntimeError):validate_index_checkpoint_retention(broken,"c",5750)


def test_requested_coherence_requires_held_and_resumed_target_captures():
    import replay_test
    native=dict(run_id='native',coherence_images={})
    executor=dict(run_id='candidate',coherence_images_requested=True,source_revision_profile='guard201',
                  host_seek_target=300,historical_advanced_tick=2504,coherence_images={})
    def add(report,tick,held,generation):
        report['coherence_images'][f'{tick}:{held}:{generation}']=dict(tick=tick,held=held,generation=generation,
            run_id=report['run_id'],gpu_complete=True)
    for tick in (300,400):
        add(native,tick,False,0)
        add(executor,tick,False,0)
    missing=replay_test.coherence_capture_coverage(native,executor)
    assert missing['result']=='fail'
    assert 'executor:held:300' in missing['missing']
    assert 'executor:native:400' in missing['missing'] # Original rendering cannot stand in for resumed rendering.
    for tick in (300,400): add(executor,tick,False,1)
    for tick in (2504,300): add(executor,tick,True,1)
    assert replay_test.coherence_capture_coverage(native,executor)['result']=='pass'
    executor['coherence_images']['300:True:1']['gpu_complete']=False
    assert replay_test.coherence_capture_coverage(native,executor)['missing']==['executor:held:300']
    executor['coherence_images_requested']=False
    assert replay_test.coherence_capture_coverage(native,executor)['result']=='not_requested'


@pytest.mark.workflow
@pytest.mark.parametrize('removed',['','--candidate-only','--host-seek','--combat-exact-advance'])
def test_initial_baseline_cli_is_bounded_before_launch(tmp_path,monkeypatch,removed):
    import replay_test as module
    args=['replay_test.py','combat-restore','--host-seek','--combat-exact-advance','--candidate-only',
          '--combat-case','commit','--combat-anchor-tick','0','--combat-advanced-tick','210','--host-seek-target','208']
    if removed: args.remove(removed)
    monkeypatch.setattr(module.sys,'argv',args)
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    entered=[]
    def boundary(*_):
        entered.append(True)
        raise RuntimeError('fixture stopped before build or live launch')
    monkeypatch.setattr(module,'ensure_build',boundary)
    if removed:
        with pytest.raises(SystemExit): module.main()
        assert not entered
    else:
        assert module.main()==1 and entered==[True]


def test_index_preparation_fallback_requires_ordered_complete_B():
    import replay_test
    rows=[
        "restore undo captured target=2504 original=11506 gpu_complete=true before_target_preparation=true",
        "seek preparation fault checkpoint=2504 B=11506 complete_B=true target_prepared=true publication=false",
        "seek preparation fallback rejected=2504 selected=170 failure=1 B_recovered=true preparation_released=true publication=false",
        "restore undo captured target=170 original=11506 gpu_complete=true before_target_preparation=true",
        "indexed checkpoint fallback run_id=c rejected=2504 selected=170 reason=retired_preparation B_recovered=true",
        "indexed checkpoint selected run_id=c tick=170 extra=2504 automatic=true",
    ]
    raw="\n".join(rows)
    assert replay_test.validate_index_preparation_fallback(raw,"c")["original_tick"]==11506
    invalid=[raw.replace("B_recovered=true","B_recovered=false"),raw.replace("original=11506","original=11505",1),
             raw.replace("failure=1","failure=0"),raw.replace("publication=false","publication=true"),
             "\n".join(rows[:2]+[rows[3],rows[2]]+rows[4:]),raw+"\n"+rows[1]]
    invalid += ["\n".join(rows[:i]+rows[i+1:]) for i in range(len(rows))]
    for bad in invalid:
        with pytest.raises(RuntimeError):replay_test.validate_index_preparation_fallback(bad,"c")


def test_index_preparation_retry_cancellation_requires_unchanged_B_and_no_fallback():
    import replay_test
    prefix=[
        "index_preparation_B_before ordinal=1 phase=engine_post sample_version=3 frame=11509 p0_vital=7",
        "seek preparation fault checkpoint=2504 B=11509 complete_B=true target_prepared=true publication=false",
        "index_preparation_B_after ordinal=1 phase=engine_post sample_version=3 frame=11509 p0_vital=7",
        "indexed preparation retry cancellation run_id=c checkpoint=2504 B=11509 elapsed_us=500 unchanged=true callbacks_unchanged=true epoch_unchanged=true fallback_started=false native_released=true",
        "indexed preparation retry renewed run_id=c target=2510 B=11509 new_request_clock=true",
    ]
    suffix=[
        "restore undo captured target=2504 original=11509 gpu_complete=true before_target_preparation=true",
        "seek preparation fault checkpoint=2504 B=11509 complete_B=true target_prepared=true publication=false",
        "seek preparation fallback rejected=2504 selected=170 failure=14 B_recovered=true preparation_released=true publication=false",
        "restore undo captured target=170 original=11509 gpu_complete=true before_target_preparation=true",
        "indexed checkpoint fallback run_id=c rejected=2504 selected=170 reason=retired_preparation B_recovered=true",
        "indexed checkpoint selected run_id=c tick=170 extra=2504 automatic=true",
    ]
    raw="\n".join(prefix+suffix)
    assert replay_test.validate_index_preparation_fallback(raw,"c",True)["retry_cancellation"]["result"]=="pass"
    bad=[raw.replace("p0_vital=7","p0_vital=8",1),raw.replace("native_released=true","native_released=false"),
         raw.replace("elapsed_us=500","elapsed_us=0"),raw.replace("target=2510 B=11509","target=2510 B=11508"),
         "\n".join(prefix[:2]+[suffix[2]]+prefix[2:]+suffix),"\n".join(suffix),
         raw.replace(prefix[1],prefix[1]+"\n"+prefix[1],1)]
    bad += ["\n".join(prefix[:i]+prefix[i+1:]+suffix) for i in range(len(prefix))]
    for text in bad:
        with pytest.raises(RuntimeError):replay_test.validate_index_preparation_fallback(text,"c",True)


def test_indexed_hud_requires_independent_target_window_and_clocks():
    from tools import replay_test
    window="[ReplayQualification] HUD observation window target=5760 first=5744 last=5880 read_only=true"
    records=[f"[ReplayQualification] HUD state tick={tick} p1=1 p2=2 combo1=3 combo2=4 timer=5 damage=0 types=1 pool=16 active=0 type_pool=16 type_active={int(tick==5761)} type_players={tick:016x} read_only=true"
             for tick in range(5761,5881)]
    native="\n".join([window]+records)
    resume="indexed seek resumed run_id=c tick=5760 target_tails_complete=true"
    # Original candidate observations must not conceal a missing or wrong
    # resumed observation. Only the independently executed suffix is compared.
    candidate="\n".join([window]+records+[resume]+records)
    result=replay_test.compare_indexed_hud(native,candidate,"c",5760)
    assert result["ticks"]==120 and result["active_type_ticks"]==1
    variants=[
        "\n".join([window]+records+[resume]+records[1:]),
        candidate.replace(records[0],records[0]+"\n"+records[0]),
        candidate.rsplit("type_players=00000000000016f8",1)[0]+"type_players=00000000000016f9 read_only=true",
        candidate.replace("last=5880","last=5881"),
        candidate.replace("target_tails_complete=true","target_tails_complete=false"),
        candidate.replace("type_pool=16 ",""),
    ]
    for bad in variants:
        with pytest.raises(RuntimeError):replay_test.compare_indexed_hud(native,bad,"c",5760)
    assert replay_test.compare_indexed_hud("legacy","legacy","c",5760)["result"]=="not_measured"
    with pytest.raises(RuntimeError):replay_test.compare_indexed_hud(native,"legacy","c",5760)


def test_indexed_same_process_hud_does_not_claim_independence(tmp_path,monkeypatch):
    from tools import replay_test
    raw=tmp_path/"candidate.log"
    raw.write_text("original\nindexed seek begin run_id=c origin=11506\nrestored")
    report={"raw_log":{"path":str(raw),"sha256":sha256_file(raw)},"run_id":"c","index_seek_target":5760}
    calls=[]
    def compare(original,whole,identity,target):
        calls.append((original,whole,identity,target));return {"result":"pass"}
    monkeypatch.setattr(replay_test,"compare_indexed_hud",compare)
    result=replay_test.validate_indexed_same_process_hud(report)
    assert calls[0][0]=="original\n" and calls[0][2:]==("c",5760)
    assert "independent control still required" in result["comparison"]
    raw.write_text("modified")
    with pytest.raises(RuntimeError,match="evidence changed"):replay_test.validate_indexed_same_process_hud(report)

@pytest.mark.parametrize('point', ['published','drained','interior'])
def test_indexed_recovery_requires_observed_B_and_exact_cancellation_boundary(point, tmp_path):
    from tools.deterministic_qualification.replay_fidelity import validate_indexed_recovery, indexed_seek_boundary_streams
    hud='actor=12/34 widget=56/78 requested=0 visibility=1 values=1,2,3,4,5,6,7 read_only=true'
    announcement='widget=90/91 enabled=1 visibility=3 active=1 clock=0,0,1,1, players=100,1,200,0,0,0,0,0,1,1,0,1065353216,0,1,1,1,0,0, native_viewport=true read_only=true'
    sample='sample_version=2 frame=500 health=12'
    keys='round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs'.split()
    common=' '.join(k+'=0' for k in keys)
    def row(n,tick):return f'[ReplayQualification] boundary ordinal={n} phase=actor_tail sample_version=2 frame={tick} {common}'
    lines=['boundaries started run_id=c native_frame=0',row(1,500),
        'index complete run_id=c entries=501 bytes=4096',
        'index closure run_id=c replay_tick=500 native_tick=500 callbacks_retained=true',
        f'index_recovery_B_before ordinal=1 phase=engine_post {sample}',
        f'indexed recovery HUD run_id=c point=B_before {hud}',
        f'indexed recovery announcement run_id=c point=B_before {announcement}',
        'indexed seek begin run_id=c origin=500 target=172 full_index=true host_checkpoint=true callbacks=1 interval=500',
        'indexed recovery HUD run_id=c point=A_published '+hud.replace('requested=0 visibility=1','requested=1 visibility=3'),
        'indexed recovery announcement run_id=c point=A_published '+announcement.replace('visibility=3','visibility=1'),
        f'indexed recovery armed run_id=c point={point} B=500 A=170 target=172 complete_B=true commit_decided=false']
    if point=='drained':
        lines+=['historical combat execution admitted run_id=c from_tick=500 to_tick=170 B_retained=true',row(2,171),row(3,172),
            'indexed recovery drained run_id=c C=172 B=500 participant=injected_render_drained complete_B=true commit_decided=false']
    if point=='interior':
        lines+=['historical combat execution admitted run_id=c from_tick=500 to_tick=170 B_retained=true',
            row(2,171),row(3,172).replace('phase=actor_tail','phase=round_sequence'),
            'completion repeat held run_id=c tick=172 elapsed_us=500001 application_updates=30 surface_frames=20 unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true',
            'indexed recovery interior queued run_id=c C=172 B=500 latest_request=B pending_repeat=true explicit_resume=false',
            row(4,173),
            'seek ownership state=Recovered B_tick=500 target=172 current_tick=500 completed_tail_tick=173 B_retained=false commit_decided=false executed_ticks=3 executed_intervals=2',
            'indexed seek already held tick=500']
    executed={'published':0,'drained':2,'interior':3}[point]
    executed_intervals=executed-(point=='interior')
    lines += [f'index_recovery_B_after ordinal=1 phase=engine_post {sample}',
        f'indexed recovery HUD run_id=c point=B_after {hud}',
        f'indexed recovery announcement run_id=c point=B_after {announcement}',
        f'indexed recovery recovered run_id=c B=500 executed_ticks={executed} executed_intervals={executed_intervals} original_recovered=true commit_decided=false',
        'indexed recovery held run_id=c B=500 elapsed_us=500001 frames=30 callbacks_unchanged=true epoch_unchanged=true HUD_unchanged=true',
        'indexed recovery released run_id=c B=500 release_completed=true authored_ticks_remaining=0 resume_requested=false cleanup=native_exit',
        'native replay exit cleanup completed requested_tick=500 recovered_tick=500 index_empty=true host_detached=true native_stop_invocations=1',
        'native session exit completed run_id=c observed_prefix_tick=499 elapsed_us=900000 native_terminate_observed=true scene=replay_list executor_disabled=true host_inactive=true',
        f'indexed recovery complete run_id=c B=500 native_tick=500 callbacks={1+executed} HUD_unchanged=true authored_continuation_unavailable=true resume_requested=false cleanup=native_exit B_observed_before_native_exit=true',
        f'boundaries completed run_id=c observations={1+executed} detached=true']
    raw='\n'.join(lines)
    proof=validate_indexed_recovery(raw,'c',point,500,170,172)
    assert proof['executed_ticks']==executed and not proof['rollback_performance_qualified']
    assert proof['announcement_B']['active_players']==1
    rows,prefix,discarded,metadata=indexed_seek_boundary_streams(raw,'c',recovery=point)
    assert len(discarded)==executed and len(prefix)==1 and metadata['continuation_ticks']==0
    from replay_test import executor_proof
    work=500+executed
    interval_work=500+executed_intervals
    accounting='\n'.join([
        'executor started run_id=c native_frame=0',
        f'world executor completed run_id=c worlds={interval_work} groups={interval_work} tasks={interval_work} manager_tasks={interval_work} manager_yields={work} idle=true disabled=true',
        f'engine executor completed run_id=c intervals={interval_work} idle=true disabled=true',
        f'application executor stopped intervals={interval_work} idle=true disabled=true',
        f'world arena completed run_id=c scopes={interval_work} max_retained_bytes=64 probe_checks=2 empty=true',
        f'executor boundaries run_id=c completed_intervals={interval_work} zero_intervals=0 multi_intervals={int(point=="interior")} repeat_requests={int(point=="interior")} move_state_ticks=0 yielded_boundaries={work} yield_every_tick=true',
        'executor completed run_id=c ticks=500 intervals=500 publications=500 native_frame=500 disabled=true'])
    def execution(text):
        path=tmp_path/'recovery-accounting.log';path.write_text(text+'\n'+accounting)
        return executor_proof({'run_id':'c','record_index':True,'index_seek':True,'index_recovery':point,
            'raw_log':{'path':str(path),'sha256':sha256_file(path)}},True)
    counts=execution(raw)
    assert counts['physical_native_frame']==500 and counts['physical_ticks']==work
    assert counts['historical_rewind_ticks']==executed
    for tick in (500,501):
        extra=raw.replace('indexed recovery held',row(2+executed,tick)+'\nindexed recovery held').replace(
            f'observations={1+executed} detached',f'observations={2+executed} detached')
        with pytest.raises(RuntimeError):execution(extra)
    changes=[('point=B_after '+hud,'point=B_after '+hud.replace('values=1,','values=9,')),
        ('point=B_after '+hud,'point=B_after '+hud.replace('requested=0','requested=1')),
        ('point=A_published','point=B_after'),('frames=30','frames=29'),('elapsed_us=500001','elapsed_us=499999'),
        ('original_recovered=true','original_recovered=false'),('release_completed=true','release_completed=false'),
        ('resume_requested=false','resume_requested=true'),('cleanup=native_exit','cleanup=resume'),
        ('native_stop_invocations=1','native_stop_invocations=2'),('host_detached=true','host_detached=false'),
        ('recovered_tick=500','recovered_tick=501'),('B_observed_before_native_exit=true','B_observed_before_native_exit=false'),
        ('index_recovery_B_after ordinal=1 phase=engine_post '+sample,'index_recovery_B_after ordinal=1 phase=engine_post '+sample.replace('health=12','health=13')),
        ('point=B_after '+hud,'point=B_after '+hud.replace('12/34','12/35'))]
    if point=='drained':changes += [('participant=injected_render_drained','participant=injected_after_cpu_settlement'),('executed_ticks=2','executed_ticks=3')]
    elif point=='interior':changes += [('completed_tail_tick=173','completed_tail_tick=172'),
        ('indexed seek already held tick=500','indexed seek already held tick=172'),
        ('pending_repeat=true','pending_repeat=false'),('application_updates=30','application_updates=29'),
        ('explicit_resume=false','explicit_resume=true'),('executed_ticks=3','executed_ticks=2')]
    else:changes += [('executed_ticks=0','executed_ticks=1')]
    for old,new in changes:
        assert old in raw
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(raw.replace(old,new),'c',recovery=point)
    marker='indexed recovery announcement run_id=c point=B_after '+announcement
    for replacement in ('',marker+'\n'+marker,marker.replace('widget=90/91','widget=90/92'),
            marker.replace('clock=0,0,1,1,','clock=1,0,1,1,'),marker.replace('players=100,','players=101,'),
            marker.replace('visibility=3','visibility=1'),marker.replace('native_viewport=true','native_viewport=false')):
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(raw.replace(marker,replacement),'c',recovery=point)
    for original,replacement in [('active=1','active=0'),('clock=0,0,1,1,','clock=0,0,1,'),
            ('1,1,1,0,0, native_viewport','1,1,1,1,0, native_viewport')]:
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(raw.replace(original,replacement),'c',recovery=point)
    with pytest.raises(RuntimeError):indexed_seek_boundary_streams(raw,'c')
    with pytest.raises(RuntimeError):indexed_seek_boundary_streams(raw,'c',recovery='drained' if point=='published' else 'published')

def test_publication_handoff_cancel_requires_active_B_and_no_native_traversal():
    from replay_test import validate_publication_handoff_cancel
    lines=['host seek publication observed run_id=c tick=170',
        'host seek phase run_id=c phase=6 pending=true commit_decided=false',
        'historical cancellation injected run_id=c point=after tick=170 execution_handed_off=true simulation_ticks=0',
        'publication render tail completed tick=170 interval=166 components=38 gameplay_traversals=0 native_jobs_joined=true GPU_drain_pending=true B_retained=true',
        'historical cancellation recovered run_id=c point=after tick=2504']
    raw='\n'.join(lines)
    assert validate_publication_handoff_cancel(raw,'c',170,2504)['authored_B_continuation_required']==120
    for bad in ('\n'.join(lines[:1]+lines[2:]),'\n'.join([lines[1],lines[0],*lines[2:]]),
        raw.replace('simulation_ticks=0','simulation_ticks=1'),raw.replace('phase=6','phase=4'),
        raw.replace(lines[2],lines[2]+'\n[ReplayQualification] boundary ordinal=1'),raw+'\n'+lines[4],raw.replace('components=38','components=0'),raw.replace('native_jobs_joined=true','native_jobs_joined=false')):
        with pytest.raises(RuntimeError):validate_publication_handoff_cancel(bad,'c',170,2504)
    with pytest.raises(RuntimeError):validate_publication_handoff_cancel(raw,'c',170,11508)


def test_automatic_end_hold_requires_native_ownership_and_no_extra_gameplay():
    from replay_test import validate_automatic_end_hold
    import pytest
    armed='replay end hold armed replay_tick=11501 native_target=11507 native_finish_returned=true application_tail_complete=true'
    held='replay end retained replay_tick=11501 native_tick=11507 completed_application=true native_bindings_valid=true surface_retained=true'
    begin='indexed seek begin run_id=c origin=11507 target=208 full_index=true'
    raw='\n'.join([armed,held,begin])
    result=validate_automatic_end_hold(raw,'c')
    assert result['last_replay_tick']==11501 and result['held_native_tick']==11507 and result['native_tail_ticks']==6
    for bad in [raw.replace(armed,''),raw+'\n'+held,raw.replace('native_target=11507','native_target=11508'),
                raw.replace('origin=11507','origin=11508'),raw.replace('native_bindings_valid=true','native_bindings_valid=false'),
                '\n'.join([armed,begin,held]),raw.replace(begin,'[ReplayQualification] boundary ordinal=44\n'+begin)]:
        with pytest.raises(RuntimeError):validate_automatic_end_hold(bad,'c')


def test_retained_index_requires_every_tail_entry_and_independent_observation(monkeypatch):
    import replay_test
    source=2
    native=[dict(frame=str(t),phase="round_sequence",round="0",round_frame=str(t),cursor="4",
                 round_state="10" if t>source else "2",source_active="false" if t>source else "true",move_state="0") for t in range(1,6)]
    candidate_rows=native[:4]
    monkeypatch.setattr(replay_test,"validate_boundary_capture",lambda text,*_:native if text.startswith('native replay') else candidate_rows)
    control="native replay endpoint run_id=n native_frame=2\nindex terminal observed run_id=n replay_tick=2 frozen=true\nindex closure run_id=n replay_tick=2 native_tick=5 callbacks_retained=true"
    rows=[]
    for t in range(5):
        rows.append(f"[ReplayQualification] index row tick={t} native_tick={t} interval={t} publications={min(t,2)} round=0 round_tick={t} cursor=4 phase={9 if t else 0} round_state={10 if t>2 else 2} source_active={0 if t>2 else 1} move_state=0")
    candidate='\n'.join(['index started run_id=c entries=1 bytes=1024 native_tick=0',*rows,
        'replay index final phase=2 entries=5 intervals=4 empty=0 multi=0 bytes=1024 native_finish=true final_tail=true',
        '[HorseMod] index closure replay_tick=2 executor_tick=4 application_idle=true',
        'index retained coverage source_tick=2 last_tick=4 tail_ticks=2 entries=5 all_native_ticks_indexed=true',
        'index complete run_id=c entries=5 bytes=1024 intervals=4 empty=0 multi=0 native_finish=true final_tail=true',
        'index retained range run_id=c source_tick=2 last_tick=4 tail_ticks=2 all_native_ticks_indexed=true',
        'index terminal observed run_id=c replay_tick=2 frozen=true',
        'index closure run_id=c replay_tick=2 native_tick=4 callbacks_retained=true',
        'index released run_id=c bytes=0 hooks_removed=true'])
    result=replay_test.validate_tick_index(control,candidate,'n','c')
    assert result['index_complete'] and result['source_end_tick']==2 and result['last_tick']==4
    assert result['native_tail_indexed'] and result['native_tail_entries']==2 and result['native_boundary_comparisons']==4
    for bad in [candidate.replace(rows[-1],''),candidate.replace('tail_ticks=2','tail_ticks=1'),
                candidate.replace('executor_tick=4','executor_tick=3'),candidate.replace('last_tick=4','last_tick=3'),
                candidate.replace('round_tick=4','round_tick=8'),candidate+'\nindex retained coverage source_tick=2 last_tick=4 tail_ticks=2 entries=5 all_native_ticks_indexed=true']:
        with pytest.raises(RuntimeError):replay_test.validate_tick_index(control,bad,'n','c')
    native.pop(3)
    with pytest.raises(RuntimeError):replay_test.validate_tick_index(control,candidate,'n','c')


def test_end_hold_v2_cannot_qualify_an_unindexed_tail():
    from replay_test import validate_automatic_end_hold
    raw='\n'.join(['replay end hold armed replay_tick=2 native_target=4 native_finish_returned=true application_tail_complete=true',
        'replay end retained replay_tick=2 native_tick=4 completed_application=true native_bindings_valid=true surface_retained=true',
        'index retained coverage source_tick=2 last_tick=4 tail_ticks=2 entries=5 all_native_ticks_indexed=true',
        'indexed seek begin run_id=c origin=4 target=1 full_index=true'])
    assert validate_automatic_end_hold(raw,'c',require_indexed_tail=True)['native_tail_indexed']
    for bad in [raw.replace('entries=5','entries=4'),raw.replace('all_native_ticks_indexed=true','all_native_ticks_indexed=false'),
                raw.replace('source_tick=2','source_tick=3')]:
        with pytest.raises(RuntimeError):validate_automatic_end_hold(bad,'c',require_indexed_tail=True)


def _supported_range_fixture():
    return '\n'.join([
        'retained source stop armed source_tick=172 target=173 native_finish=false unsupported_native_tail=true',
        'retained source stop complete source_tick=172 last_tick=173 entries=174 intervals=173 empty=0 multi=0 bytes=16384 native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true',
        'index supported complete run_id=c entries=174 bytes=16384 intervals=173 empty=0 multi=0 native_finish=false unsupported_native_tail=true',
        'index supported range run_id=c source_tick=172 last_tick=173 tail_ticks=1 all_retained_ticks_indexed=true unsupported_native_tail=true',
        'indexed seek begin run_id=c origin=173 target=170 supported_index=true'])


def test_retained_source_range_requires_explicit_tail_and_ordered_hold():
    from replay_test import validate_automatic_end_hold
    from deterministic_qualification.replay_fidelity import retained_source_range
    raw=_supported_range_fixture()
    assert retained_source_range(raw,'c')==dict(source_tick=172,last_tick=173,entries=174,unsupported_native_tail=True,full_native_replay=False)
    assert validate_automatic_end_hold(raw,'c')['unsupported_native_tail']
    for bad in [raw.replace('unsupported_native_tail=true','unsupported_native_tail=false'),
                raw.replace('entries=174','entries=173'),raw.replace('target=173','target=174'),
                raw.replace('run_id=c','run_id=other'),raw.replace('tail_ticks=1','tail_ticks=0'),
                raw+'\nnative match finished run_id=c boundary=ReplayBattleScene.OnFinishMatch.post',
                raw.replace('indexed seek begin','[ReplayQualification] boundary ordinal=700\nindexed seek begin')]:
        with pytest.raises(RuntimeError):validate_automatic_end_hold(bad,'c')


def test_retained_trajectory_never_reports_full_native_completion():
    from deterministic_qualification.replay_fidelity import validate_trajectory_completion
    raw='\n'.join([_supported_range_fixture(),
        'retained replay endpoint run_id=c native_frame=172 round=0 cursor=4 world_mode=5 round_state=5 source_active=false native_finish=false unsupported_native_tail=true',
        'ordered round outcomes verified rounds=1 match_winner=0 winners=0',
        '[ReplayQualification] recorded match outcome run_id=c recorded=0 simulated=0 equal=true',
        'trajectory completed run_id=c observations=172 full_match=false'])
    result=validate_trajectory_completion(raw,'c',36000,True)
    assert not result['full_match'] and not result['native_match_finished'] and result['retained_range']['last_tick']==173
    for bad in [raw.replace('full_match=false','full_match=true'),raw.replace('native_frame=172','native_frame=171'),
                raw.replace('winners=0','winners=1,0'),raw.replace('simulated=0','simulated=1')]:
        with pytest.raises(RuntimeError):validate_trajectory_completion(bad,'c',36000,True)


def test_supported_index_stream_keeps_actual_rewind_contract():
    from deterministic_qualification.replay_fidelity import indexed_seek_boundary_streams
    keys='round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs'.split()
    common=' '.join(k+'=0' for k in keys)
    def row(n,t):return f'[ReplayQualification] boundary ordinal={n} phase=actor_tail sample_version=2 frame={t} {common}'
    proof=_supported_range_fixture().replace('172','499').replace('173','500').replace('174','501').split('\nindexed seek begin')[0]
    raw='\n'.join(['boundaries started run_id=c native_frame=0',row(1,500),proof,
        'index closure run_id=c replay_tick=499 native_tick=500 callbacks_retained=true',
        'indexed seek begin run_id=c origin=500 target=208 supported_index=true host_checkpoint=true callbacks=1 interval=500',
        'historical combat execution admitted run_id=c from_tick=500 to_tick=170 B_retained=true',
        row(2,170),row(3,328),'indexed seek continuation run_id=c first=209 last=328 ticks=120',
        'boundaries completed run_id=c observations=3 detached=true'])
    assert indexed_seek_boundary_streams(raw,'c')[3]['origin']==500
    for bad in [raw.replace('supported_index=true','full_index=true'),raw.replace('from_tick=500','from_tick=499'),
                raw.replace('callbacks=1','callbacks=2')]:
        with pytest.raises(RuntimeError):indexed_seek_boundary_streams(bad,'c')


def test_retained_index_compares_every_supported_tick_without_claiming_native_tail(monkeypatch):
    import replay_test
    native=[dict(frame=str(t),phase='round_sequence',round='0',round_frame=str(t),cursor='4',
                 round_state='5' if t>=172 else '2',source_active='false' if t>=172 else 'true',move_state='0') for t in range(1,177)]
    monkeypatch.setattr(replay_test,'validate_boundary_capture',lambda text,*_:native if text.startswith('native replay') else native[:173])
    control='native replay endpoint run_id=n native_frame=175\nindex terminal observed run_id=n replay_tick=175 frozen=true\nindex closure run_id=n replay_tick=175 native_tick=176 callbacks_retained=true'
    rows=[f'[ReplayQualification] index row tick={t} native_tick={t} interval={t} publications={t} round=0 round_tick={t} cursor=4 phase={9 if t else 0} round_state={5 if t>=172 else 2} source_active={0 if t>=172 else 1} move_state=0' for t in range(174)]
    candidate='\n'.join(['index started run_id=c entries=1 bytes=16384 native_tick=0',*rows,_supported_range_fixture(),
        'index terminal observed run_id=c replay_tick=172 frozen=true',
        'index closure run_id=c replay_tick=172 native_tick=173 callbacks_retained=true',
        'index released run_id=c bytes=0 hooks_removed=true'])
    result=replay_test.validate_tick_index(control,candidate,'n','c')
    assert result['entries']==174 and result['native_boundary_comparisons']==173
    assert not result['full_native_replay'] and result['unsupported_tail']['first_tick']==174
    for bad in [candidate.replace(rows[100],''),candidate.replace('round_tick=100','round_tick=99'),
                candidate.replace('native_tick=173 callbacks','native_tick=174 callbacks')]:
        with pytest.raises(RuntimeError):replay_test.validate_tick_index(control,bad,'n','c')


def test_completion_repeat_requires_matching_intervention_and_external_hold():
    from replay_test import validate_completion_repeat
    reports=[dict(run_id="n",completion_repeat=True),dict(run_id="c",completion_repeat=True)]
    native="completion repeat intervention run_id=n tick=208 ordinal=1 native_repeat=1"
    candidate="\n".join(["completion repeat intervention run_id=c tick=208 ordinal=1 native_repeat=1",
        "completion repeat intervention run_id=c tick=208 ordinal=2 native_repeat=1",
        "seek exact completion deferred tick=208 simulation_phase=8 pending_task=true B_retained=true resume_required=true",
        "completion repeat held run_id=c tick=208 elapsed_us=500001 application_updates=31 surface_frames=20 unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true",
        "historical single step requested run_id=c origin=208 target=209 dispatch=host_api B_retained=true"])
    assert validate_completion_repeat([native,candidate],reports)["result"]=="pass"
    for bad in [candidate.replace("application_updates=31","application_updates=1"),candidate.replace("tick=208 elapsed","tick=209 elapsed"),
                candidate.replace("B_retained=true","B_retained=false"),candidate.replace("ordinal=2","ordinal=1"),
                candidate.replace("dispatch=host_api","dispatch=implicit")]:
        with pytest.raises(RuntimeError):validate_completion_repeat([native,bad],reports)
    with pytest.raises(RuntimeError):validate_completion_repeat([native,candidate],[dict(run_id="n"),reports[1]])


def test_seek_commit_requires_exact_tails_after_deferred_completion():
    from replay_test import validate_seek_commit_ownership
    states=[("BRetained",208,210),("APublished",208,205),("ExecutionActive",208,205),("CSettled",208,208),
            ("CompletingTargetTails",208,208),("CSettled",208,208),("ExecutionActive",209,208),("CSettled",209,209),
            ("CompletingTargetTails",209,209),("TargetTailsCompleted",209,209),("Committing",209,209),("Committed",209,209)]
    lines=[]
    for i,(phase,target,tick) in enumerate(states):
        complete=i>=9;commit=i>=10
        lines.append(f"seek ownership state={phase} B_tick=210 target={target} current_tick={tick} completed_tail_tick={209 if complete else 0} B_retained={str(not commit).lower()} commit_decided={str(commit).lower()} executed_ticks={4 if complete else 0} executed_intervals={3 if complete else 0}")
    raw="\n".join(lines)
    assert validate_seek_commit_ownership(raw,205,210,208,True,True)["completed_tail_tick"]==209
    for bad in [raw.replace("current_tick=209 completed_tail_tick=209","current_tick=210 completed_tail_tick=210"),
                raw.replace(lines[5],lines[5].replace("B_retained=true","B_retained=false")),
                raw.replace(lines[4]+"\n","")]:
        with pytest.raises(RuntimeError):validate_seek_commit_ownership(bad,205,210,208,True,True)
    with pytest.raises(RuntimeError):validate_seek_commit_ownership(raw,205,210,208,True)


def test_deferred_completion_cancellation_requires_full_B_ownership_chain():
    from replay_test import validate_seek_recovery_ownership, validate_completion_repeat
    states=['BRetained','APublished','ExecutionActive','CSettled','CompletingTargetTails','CSettled','RecoveryQuiescing','Recovering','Recovered']
    ticks=[210,205,205,208,208,208,208,209,210]
    lines=[]
    for i,(state,tick) in enumerate(zip(states,ticks)):
        lines.append(f'seek ownership state={state} B_tick=210 target=208 current_tick={tick} completed_tail_tick={209 if i>=7 else 0} B_retained={str(i!=8).lower()} commit_decided=false executed_ticks={4 if i>=7 else 0} executed_intervals={3 if i>=7 else 0}')
    lines.insert(7,'seek C-only retirement owners=0 completed=0 B_retained=true native_render_drain_required=true')
    lines.insert(8,'emitter native retirement settled kind=cpu component=abc ordinal=1 destructor_completed=true slot_null=true B_retained=true C_freed_by_native=true')
    raw='\n'.join(lines)
    result=validate_seek_recovery_ownership(raw,205,210,208,208,deferred=True)
    assert result['completed_C_tick']==209 and result['B_recovered_tick']==210
    for bad in [raw.replace('commit_decided=false','commit_decided=true'),raw.replace('current_tick=210 completed_tail_tick=209','current_tick=209 completed_tail_tick=209'),raw.replace('destructor_completed=true','destructor_completed=false'),raw.replace('state=CSettled','state=Committed',1)]:
        with pytest.raises(RuntimeError): validate_seek_recovery_ownership(bad,205,210,208,208,deferred=True)
    native='completion repeat intervention run_id=n tick=208 ordinal=1 native_repeat=1'
    candidate='\n'.join(['completion repeat intervention run_id=c tick=208 ordinal=1 native_repeat=1','completion repeat intervention run_id=c tick=208 ordinal=2 native_repeat=1','seek exact completion deferred tick=208 simulation_phase=8 pending_task=true B_retained=true resume_required=true','completion repeat held run_id=c tick=208 elapsed_us=500001 application_updates=31 surface_frames=20 unchanged=true pending_task=true B_retained=true callbacks_unchanged=true release_requires_resume=true','historical cancellation injected run_id=c point=after tick=208 completion_deferred=true dispatch=host_api'])
    reports=[dict(run_id='n',completion_repeat=True),dict(run_id='c',completion_repeat=True,historical_cancel='after')]
    assert validate_completion_repeat([native,candidate],reports)['cancellation_boundary']=='CompletionBlocked'
    with pytest.raises(RuntimeError): validate_completion_repeat([native,candidate.replace('completion_deferred=true','completion_deferred=false')],reports)


def test_deferred_cancellation_plan_includes_A_execution():
    from deterministic_qualification.replay_control import expected_historical_rewinds
    assert expected_historical_rewinds(dict(completion_repeat=True,historical_cancel="after"))==[(210,205)]


def test_deferred_cancellation_keeps_restored_render_generation():
    from replay_test import continuation_generation
    assert continuation_generation(dict(completion_repeat=True),'after')==1
    assert continuation_generation({},'after')==0


@pytest.mark.native_contract
def test_native_session_exit_orders_recovery_before_publication(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    header=(module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.hpp').read_text()
    declarations=header[header.index('    enum class SessionExitPhase'):header.index('    static bool ReadSessionExit')]
    (tmp_path / 'replay_session_exit_declarations.inl').write_text(declarations)
    source=(module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.SessionExit.inl').read_text()
    (tmp_path / 'replay_session_exit_service.inl').write_text(source[source.index('bool Sc6ReplayHost::ServiceSessionExit()'):])
    fixture=module.ROOT / 'tools/replay_session_exit_selftest.cpp'
    includes=module.ROOT / 'HorseMod/horselib'
    generated=module.BUILD / 'HorseMod/generated'
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{includes}" /I"{generated}" "{fixture}" /Fe:session-exit.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / 'session-exit.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert 'session exit ordering and retention passed' in checked.stdout


@pytest.mark.parametrize('mutation',['none','missing_drain','duplicate','wrong_order','wrong_tick','blocked'])
def test_native_session_exit_requires_ordered_native_cleanup(mutation):
    from replay_test import validate_native_session_exit
    rows=[
        'native session exit requested run_id=c observed_prefix_tick=600 index_entries=601 checkpoint170_retained=true cleanup_owner=host',
        'native replay exit deferred tick=610 admitted=true original_invocations=0',
        'native replay exit cleanup completed requested_tick=610 recovered_tick=611 index_empty=true host_detached=true native_stop_invocations=1',
        'native session exit completed run_id=c observed_prefix_tick=600 elapsed_us=5000 native_terminate_observed=true scene=replay_list executor_disabled=true host_inactive=true',
    ]
    if mutation=='missing_drain': rows.pop(2)
    if mutation=='duplicate': rows.append(rows[2])
    if mutation=='wrong_order': rows[1],rows[2]=rows[2],rows[1]
    if mutation=='wrong_tick': rows[2]=rows[2].replace('requested_tick=610','requested_tick=609')
    if mutation=='blocked': rows.append('native replay exit blocked')
    if mutation=='none': assert validate_native_session_exit('\n'.join(rows),'c')['result']=='pass'
    else:
        with pytest.raises(RuntimeError): validate_native_session_exit('\n'.join(rows),'c')


@pytest.mark.workflow
@pytest.mark.parametrize('host_session',[False,True])
def test_session_exit_cli_does_not_claim_held_cancel_or_full_index(tmp_path,monkeypatch,host_session):
    import json
    import replay_test as module
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module.sys,'argv',['replay_test.py','session-exit','--samples','600']+(['--host-session'] if host_session else []))
    monkeypatch.setattr(module,'ensure_build',lambda *_:{})
    monkeypatch.setattr(module,'sha256_file',lambda *_:'fixture')
    monkeypatch.setattr(module,'require_bootstrap',lambda *_:None)
    (tmp_path/'baseline-a.json').write_text('{}')
    raw=tmp_path/'raw.log';raw.write_text('fixture')
    calls=[]
    def args(*values):return SimpleNamespace(report=tmp_path/(values[2]+'.json'))
    def capture(options):
        assert options.native_session_exit and not options.index_cancel
        assert options.host_session==host_session
        calls.append(options)
        report={'run_id':'fixture','raw_log':{'path':str(raw)}}
        options.report.write_text(json.dumps(report));return report
    monkeypatch.setattr(module,'control_args',args)
    monkeypatch.setattr(module,'capture',capture)
    monkeypatch.setattr(module,'validate_intro_skips',lambda *_:{})
    monkeypatch.setattr(module,'validate_native_session_exit',lambda *_:{'result':'pass'})
    monkeypatch.setattr(module,'validate_host_session_entry',lambda *_:{'result':'pass'})
    monkeypatch.setattr(module,'compare_passive_controls',lambda *_,**__:{'result':'pass'})
    monkeypatch.setattr(module,'executor_proof',lambda *_:{})
    monkeypatch.setattr(module,'validate_boundary_capture',lambda *_:[])
    monkeypatch.setattr(module,'verify_executor_native_counts',lambda *_:None)
    def wrong_scope(*_):raise AssertionError('wrong qualification scope')
    monkeypatch.setattr(module,'validate_tick_index',wrong_scope)
    monkeypatch.setattr(module,'validate_held_index_cancellation',wrong_scope)
    assert module.main()==0
    assert len(calls)==2 and calls[0].executor


@pytest.mark.parametrize('fault',['none','missing','duplicate','reverse','intro','rejected'])
def test_host_session_entry_receipts(fault):
    from replay_test import validate_host_session_entry
    entry='automatic replay session indexed native_tick=0 entries=1 source_round=-1 source_cursor=0 executor_owner=host checkpoint_placement=host\n'
    placement='initial index checkpoint selected origin=169 target=170 native_phase_tick=6 placement_owner=host\n'
    raw=entry+placement
    if fault=='missing':raw=placement
    if fault=='duplicate':raw+=entry
    if fault=='reverse':raw=placement+entry
    if fault=='intro':raw=raw.replace('native_phase_tick=6','native_phase_tick=0')
    if fault=='rejected':raw+='automatic replay entry rejected'
    if fault=='none':assert validate_host_session_entry(raw)['checkpoint_tick']==170
    else:
        with pytest.raises(RuntimeError):validate_host_session_entry(raw)

def test_managed_second_checkpoint_requires_completed_native_placement():
    from replay_test import validate_host_index_checkpoint
    lines=['next-round index checkpoint selected origin=2503 target=2504 native_phase_tick=6 placement_owner=host',
        'host index checkpoint armed run_id=c origin=2503 target=2504',
        'host index checkpoint adopted run_id=c tick=2504 capture_retention_and_retirement=host observer=read_only',
        'index checkpoint owned tick=2504 retained=2',
        'index checkpoint retained run_id=c tick=2504 entries=2505',
        'index checkpoint capture retired run_id=c tick=2504 immutable_owner_retained=true',
        'host index checkpoint completed run_id=c tick=2504 pending=false cancelled=false']
    raw='\n'.join(lines)
    assert validate_host_index_checkpoint(raw,'c',False,True,tick=2504)['completed_resume']
    for i in range(len(lines)):
        with pytest.raises(RuntimeError):validate_host_index_checkpoint('\n'.join(lines[:i]+lines[i+1:]),'c',False,True,tick=2504)
        with pytest.raises(RuntimeError):validate_host_index_checkpoint(raw+'\n'+lines[i],'c',False,True,tick=2504)
    for old,new in [('native_phase_tick=6','native_phase_tick=0'),('origin=2503','origin=2502'),
                    ('retained=2','retained=1'),('observer=read_only','observer=capture_owner'),('pending=false','pending=true')]:
        with pytest.raises(RuntimeError):validate_host_index_checkpoint(raw.replace(old,new),'c',False,True,tick=2504)
    with pytest.raises(RuntimeError):validate_host_index_checkpoint('\n'.join(reversed(lines)),'c',False,True,tick=2504)


@pytest.mark.workflow
@pytest.mark.parametrize('flags,allowed',[
    (['equivalence','--full-match','--index-seek','--host-session'],True),
    (['equivalence','--host-session'],False),
    (['equivalence','--full-match','--index-seek','--host-session','--index-extra-checkpoint','180'],False),
    (['equivalence','--full-match','--index-seek','--host-session','--index-extra-checkpoint','2504','--index-seek-target','2510'],True),
    (['equivalence','--full-match','--index-seek','--host-session','--index-recovery','interior','--completion-repeat'],True),
    (['equivalence','--full-match','--index-seek','--host-session','--index-recovery','interior'],False),
    (['equivalence','--full-match','--index-seek','--host-session','--index-recovery','interior','--completion-repeat','--index-seek-target','214'],False),
])
def test_managed_index_cli_scope(tmp_path,monkeypatch,flags,allowed):
    import replay_test as module
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module.sys,'argv',['replay_test.py']+flags)
    calls=[]
    def reached_build(*args):
        calls.append(args)
        raise RuntimeError('focused admission fixture ends before building')
    monkeypatch.setattr(module,'ensure_build',reached_build)
    if allowed:
        assert module.main()==1 and calls
    else:
        with pytest.raises(SystemExit):module.main()
        assert not calls


@pytest.mark.parametrize('mode',['matched','mixed','full','both'])
def test_native_exit_comparison_admission_preserves_protocol_scope(tmp_path,mode):
    import json
    from deterministic_qualification.replay_fidelity import compare_passive_controls
    paths=[]
    for candidate in (False,True):
        report=dict(index_checkpoint=candidate,native_session_exit=True,index_cancel=False,
                    record_index=True,skip_intros=True,native_fixed_seed=True,serial_particles=True,full_match=False)
        if mode=='mixed' and candidate:report['native_session_exit']=False
        if mode=='full':report['full_match']=True
        if mode=='both':report['index_cancel']=True
        path=tmp_path/('candidate.json' if candidate else 'native.json');path.write_text(json.dumps(report));paths.append(path)
    # Matching bounded protocols pass admission, then still require actual
    # mapped-binary/cleanup proof. Other protocols reject before that point.
    error='execution or cleanup proof' if mode=='matched' else 'matching explicit native intervention'
    with pytest.raises(RuntimeError,match=error):compare_passive_controls(*paths,index_checkpoint=True)


@pytest.mark.native_contract
def test_production_capture_requires_particle_completion_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Checkpoint.inl').read_text()
    anchor='L"vfx_requests");\n    }\n'
    start=source.index(anchor)+len(anchor)
    end=source.index('    if (!checkpoint_capture_)',start)
    (tmp_path/'completion_capture_boundary.inl').write_text(source[start:end])
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl').read_text()
    anchor='    measure("traces");\n'
    start=source.index(anchor,source.index('Status Sc6ReplayHost::CaptureHistoricalExecution'))+len(anchor)
    end=source.index('    status=execution.hud.Capture',start)
    (tmp_path/'completion_execution_boundary.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_completion_capture_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:completion.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'completion.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_vfx_gpu_dependencies_reach_object_lease(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text()
    start=source.index('Status Sc6ReplayVfxState::RetainOwners(')
    end=source.index('Status Sc6ReplayVfxState::CaptureTilePools(',start)
    validation=source.index('Status Sc6ReplayVfxState::ValidateReconstructionDependencies()')
    validation_end=source.index('Status Sc6ReplayVfxState::VisitSingleGpuComponents(',validation)
    bindings=source.index('Status Sc6ReplayVfxState::ValidateReconstructionBindings(')
    bindings_end=source.index('Status Sc6ReplayVfxState::PrepareReconstructionBindings(',bindings)
    (tmp_path/'vfx_retain_owners.inl').write_text(source[start:end]+source[validation:validation_end]+source[bindings:bindings_end])
    trace=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp').read_text(encoding='utf-8')
    start=trace.index('    bool RetainsCapturedMesh(')
    (tmp_path/'trace_capture_mesh_ownership.inl').write_text(trace[start:trace.index('    // Failure-only inventory',start)],encoding='utf-8')
    host=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Checkpoint.inl').read_text(encoding='utf-8')
    start=host.index('        if(capture_copy) for(auto* object:capture_copy->lighting_owners())')
    (tmp_path/'host_lighting_companions.inl').write_text(host[start:host.index('        for (std::size_t i = 0; i < output.physics.creation_owner_count;',start)],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_vfx_lease_boundary_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:lease.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'lease.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_particle_lifetime_guard(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    import pefile
    pe=pefile.PE(str(module.GAME_ROOT / "SoulcaliburVI.exe"),fast_load=True)
    data=pe.get_data(0x1f9bc90,32)
    assert data.hex()=="8b819002000085c074133981700100007c0b83b918010000000f94c0c332c0c3"
    (tmp_path / "native_gpu_completion_bytes.inl").write_text("static constexpr unsigned char native_gpu_completion[]={"+",".join(map(str,data))+"};")
    fixture=module.ROOT / "tools/replay_particle_lifetime_guard_selftest.cpp"
    includes=module.ROOT / "HorseMod/horselib"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{includes}" "{fixture}" /Fe:lifetime.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / "lifetime.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr
    assert "particle lifecycle selection and recovery ownership passed" in checked.stdout


def test_natural_particle_lifetime_recovery_receipt():
    from deterministic_qualification.replay_fidelity import expected_recovery_rewind, particle_lifetime_abort_tick
    report=dict(seek_advance_failure=True,historical_anchor_tick=205,historical_advanced_tick=210,host_seek_target=220,historical_cancel="after")
    receipt="seek protected particle lifetime abort component=abcd routes=1 tick=218 target=220 B=210 before_native_lifecycle=true B_retained=true commit_decided=false"
    assert expected_recovery_rewind(report,receipt)==(218,210,220)
    for bad in ("",receipt+"\n"+receipt,receipt.replace("routes=1","routes=4"),receipt.replace("tick=218","tick=221"),receipt.replace("B_retained=true","B_retained=false")):
        with pytest.raises(RuntimeError): particle_lifetime_abort_tick(bad)


@pytest.mark.parametrize("native_B_callback",[False,True])
def test_execution_fallback_requires_recovery_between_publications(native_B_callback):
    from deterministic_qualification.replay_fidelity import validate_boundary_capture,execution_fallback_receipt
    fields="round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs"
    common=" ".join(f"{key}=0" for key in fields.split())
    lines=["boundaries started run_id=r native_frame=210"]
    count=0
    def row(tick):
        nonlocal count
        count+=1
        lines.append(f"[ReplayQualification] boundary ordinal={count} phase=actor_tail sample_version=2 frame={tick} {common}")
    row(210)
    lines.append("historical combat execution admitted run_id=r from_tick=210 to_tick=205 B_retained=true")
    row(206);row(217)
    lines.extend([
        "seek protected particle lifetime abort component=abcd routes=1 tick=217 target=220 B=210 before_native_lifecycle=true B_retained=true commit_decided=false",
        "seek ownership state=Recovered B_tick=210 target=220 current_tick=210 completed_tail_tick=217 B_retained=false commit_decided=false executed_ticks=12 executed_intervals=12",
        "seek execution fallback rejected=205 selected=170 target=220 B=210 discarded_ticks=12 discarded_intervals=12 B_recovered=true transaction_released=true publication=true unchanged_input_history=true",
        "execution fallback B verified run_id=r tick=210 checkpoint=170 target=220 discarded_ticks=12 discarded_intervals=12 independent_original_equal=true commit_decided=false"])
    if native_B_callback:row(210)
    lines.append("historical combat execution admitted run_id=r from_tick=210 to_tick=170 B_retained=true")
    row(171);row(220);row(340)
    lines.append(f"boundaries completed run_id=r observations={count} detached=true")
    raw="\n".join(lines)
    args=dict(historical_restore=True,expected_rewinds=[(210,205),(210,170)],retained_execution=True,execution_retry=True)
    assert len(validate_boundary_capture(raw,"r",**args))==count
    assert execution_fallback_receipt(raw,"r")["discarded_ticks"]==12
    for before,after in [("B_recovered=true","B_recovered=false"),("independent_original_equal=true","independent_original_equal=false"),
                         ("transaction_released=true","transaction_released=false"),("discarded_ticks=12","discarded_ticks=11"),
                         ("completed_tail_tick=217","completed_tail_tick=216"),("routes=1","routes=4")]:
        with pytest.raises(RuntimeError):validate_boundary_capture(raw.replace(before,after),"r",**args)
    with pytest.raises(RuntimeError):validate_boundary_capture(raw,"r",**{**args,"execution_retry":False})


@pytest.mark.workflow
@pytest.mark.parametrize("extra,allowed",[([],True),(["--seek-advance-failure"],False),(["--corrected-inputs"],False)])
def test_execution_fallback_cli_preserves_B210(tmp_path,monkeypatch,extra,allowed):
    import replay_test as module
    monkeypatch.setattr(module,"OUTPUT",tmp_path)
    monkeypatch.setattr(module.sys,"argv",["replay_test.py","combat-restore","--combat-case","commit","--host-seek",
        "--combat-anchor-tick","170","--combat-advanced-tick","210","--host-seek-target","220",
        "--checkpoint-fallback","--combat-exact-advance"]+extra)
    reached=[]
    def stop(*args):
        reached.append(True);raise RuntimeError("fixture stops before build")
    monkeypatch.setattr(module,"ensure_build",stop)
    if allowed: assert module.main()==1 and reached
    else:
        with pytest.raises(SystemExit):module.main()
        assert not reached


def test_execution_callback_segments_preserve_order_multiplicity_and_values():
    import replay_test as module
    native=[dict(ordinal=str(i),frame=str(tick),phase=phase,value=str(tick))
            for i,(tick,phase) in enumerate(( (tick,phase) for tick in range(170,341)
                                            for phase in ("callback_a30","actor_tail")),1)]
    original=[row.copy() for row in native if int(row["frame"])<=210]
    discarded=[row.copy() for row in native if 205<int(row["frame"])<=217]
    final=[row.copy() for row in native if 170<int(row["frame"])<=340]
    candidate=original+discarded+final
    assert module.compare_execution_callback_segments(native,candidate,340)==len(candidate)
    for change in ("duplicate","omit","swap","state"):
        bad=[row.copy() for row in candidate]
        index=len(original)+len(discarded)+15
        if change=="duplicate":bad.insert(index,bad[index].copy())
        elif change=="omit":bad.pop(index)
        elif change=="swap":bad[index],bad[index+1]=bad[index+1],bad[index]
        else:bad[index]["value"]="different"
        with pytest.raises(RuntimeError,match="native callback mismatch"):
            module.compare_execution_callback_segments(native,bad,340)


def test_execution_fallback_request_interval_matches_cli_scope():
    from deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=210,checkpoint_pair=False,
                checkpoint_fallback=True,host_seek_target=220)
    assert historical_capture_interval_supported(report)
    for change in (dict(checkpoint_fallback=False),dict(historical_cancel="after"),dict(host_seek_target=214),
                   dict(corrected_inputs=True),dict(host_seek_repeat=True),dict(seek_advance_failure=True)):
        assert not historical_capture_interval_supported({**report,**change})
    assert historical_capture_interval_supported({**report,"historical_advanced_tick":220,"checkpoint_fallback":False})


@pytest.mark.parametrize("chosen,target", [(170,208),(2511,5574),(6415,6417)])
def test_three_managed_checkpoint_inventory_and_exact_selection(chosen,target):
    from replay_test import validate_index_checkpoint_retention
    rows=[]
    for ordinal,tick in enumerate((170,2511,6415),1):
        kind=("initial","next-round","later-middle")[ordinal-1]
        rows.extend((f"{kind} index checkpoint selected origin={tick-1} target={tick} native_phase_tick=6 placement_owner=host",
            f"host index checkpoint armed run_id=c origin={tick-1} target={tick}",
            f"host index checkpoint adopted run_id=c tick={tick} capture_retention_and_retirement=host observer=read_only",
            f"index checkpoint owned tick={tick} retained={ordinal} owned_bytes=525000000",
            f"index checkpoint retained run_id=c tick={tick} entries={tick+1} elapsed_us=80000 owned_bytes=525000000 callbacks_unchanged=true application_idle=true",
            f"index checkpoint capture retired run_id=c tick={tick} immutable_owner_retained=true",
            f"host index checkpoint completed run_id=c tick={tick} pending=false cancelled=false"))
    rows.extend(("index complete run_id=c",f"indexed seek begin run_id=c origin=11149 target={target} supported_index=true host_checkpoint=true",
        f"indexed checkpoint selected run_id=c tick={chosen} extra=6415 automatic=true"))
    for tick in (170,2511,6415):
        rows.append(f"index checkpoint release witness tick={tick} current_tick=11150 vfx_owners_live=false target_shape_supported=true owned_bytes=525000000")
    rows.extend(("seek checkpoint retired tick=11150 application_idle=true","index released run_id=c bytes=0 hooks_removed=true"))
    raw="\n".join(rows)
    result=validate_index_checkpoint_retention(raw,"c",2504)
    assert result['additional_checkpoint']['selected_tick']==chosen
    assert len(result['additional_checkpoint']['checkpoints'])==3
    if chosen==170:
        rewound=raw.replace('tick=2511 current_tick=11150','tick=2511 current_tick=448')
        assert validate_index_checkpoint_retention(rewound,'c',2504)['additional_checkpoint']['selected_tick']==170
        early=rewound.replace('index complete run_id=c','')
        with pytest.raises((RuntimeError,ValueError)):validate_index_checkpoint_retention(early,'c',2504)
    for i in range(len(rows)):
        with pytest.raises((RuntimeError,ValueError)):
            validate_index_checkpoint_retention("\n".join(rows[:i]+rows[i+1:]),"c",2504)
    for broken in (raw.replace('retained=3','retained=2'),raw.replace('525000000','1073741825'),
                   raw.replace('native_phase_tick=6','native_phase_tick=5'),
                   raw.replace(f'tick={chosen} extra=6415',f'tick={chosen+1} extra=6415'),
                   raw.replace('entries=6416','entries=6415')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(broken,"c",2504)
    # A known earlier owner is allowed only with an explicit rejection witness.
    if chosen==6415:
        earlier=raw.replace('tick=6415 extra=6415','tick=2511 extra=6415')
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(earlier,"c",2504)
        earlier+='\nindexed checkpoint fallback run_id=c rejected=6415 selected=2511 reason=retained_owner_lifetime'
        assert validate_index_checkpoint_retention(earlier,"c",2504)['additional_checkpoint']['automatic_fallback']



@pytest.mark.native_contract
def test_production_restore_preparation_cancellation_stops_before_CPU_acquisition(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl").read_text(encoding="utf-8")
    start=source.index("void Sc6ReplayHost::AdvanceRestorePreparation() noexcept")
    end=source.index("    try {",start)
    call_start=source.index("        const auto status=PrepareHistoricalRestore(*transaction.target);",end)
    call_end=source.index("        transaction.preparing=false;",call_start)
    rejected=source[call_start:call_end].replace("PrepareHistoricalRestore(*transaction.target)","RejectCpuPreparation()")
    (tmp_path / "replay_restore_preparation_prefix.inl").write_text(
        source[start:end]+"    ++cpu_entries;\n    if(cpu_failure) {\n"+rejected+"    }\n}\n",encoding="utf-8")
    step_start=source.index("    const auto step =",source.index("Status Sc6ReplayHost::PrepareHistoricalRestore"))
    step_end=source.index("    auto status =",step_start)
    (tmp_path / "replay_preparation_failure_step.inl").write_text(source[step_start:step_end],encoding="utf-8")
    recovered=source.index('    if (operation.phase == RestoreOperationPhase::Recovered)')
    recovered_end=source.index('        const auto ground=transaction.ground.RecoverBodies();',recovered)
    (tmp_path/'replay_unexecuted_recovery.inl').write_text(source[recovered:recovered_end]+'    }\n',encoding='utf-8')
    surface=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Surface.inl').read_text()
    guard=surface.index('    if(action==ParticleCopyAction::DrainPrivateOwnerRetirement')
    guard_end=surface.index('    if(action==ParticleCopyAction::CompleteParticleRenderOwners',guard)
    (tmp_path/'replay_private_retirement_admission.inl').write_text(surface[guard:guard_end],encoding='utf-8')
    fixture=module.ROOT / "tools/replay_restore_preparation_selftest.cpp"
    ground=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl").read_text(encoding="utf-8")
    begin=ground.index("Sc6ReplayGroundDebrisState::Progress Sc6ReplayGroundDebrisState::RecoverBodies()")
    end=ground.index("Status Sc6ReplayGroundDebrisState::ReleaseRecoveredOwners()",begin)
    (tmp_path / "replay_ground_recovery_method.inl").write_text(ground[begin:end],encoding="utf-8")
    begin=ground.index("    bool engine_physics_quiescent() const noexcept {")
    end=ground.index('#include "ReplayGroundSceneLock.inl"',begin)
    queue=ground[begin:end].replace("    bool engine_physics_quiescent() const noexcept", "bool GroundQueueAdmission::Read() const noexcept",1)
    (tmp_path / "replay_ground_queue_method.inl").write_text(queue,encoding="utf-8")
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:preparation.exe /Fo:fixture.obj\n',encoding="utf-8")
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "preparation.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_capture_reopen_and_empty_lighting_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT / "HorseMod/horselib/deterministic"
    source=(root / "Sc6ReplayParticleCopy.Capture.inl").read_text()
    functions=source[source.index("std::size_t Sc6ReplayParticleCopy::reopen_shared_bytes"):source.index("std::size_t Sc6ReplayParticleCopy::retained_capture_bytes")]
    functions+=source[source.index("bool Sc6ReplayParticleCopy::BeginFromCapture"):]
    (tmp_path / "replay_capture_reopen_methods.inl").write_text(functions)
    lighting=(root / "Sc6ReplayParticleCopy.Lighting.inl").read_text()
    (tmp_path / "replay_lighting_release_method.inl").write_text(lighting[lighting.index("bool Sc6ReplayParticleCopy::FinishLighting"):lighting.index("bool Sc6ReplayParticleCopy::LightingPrivateUndoMatches")])
    fixture=module.ROOT / "tools/replay_capture_reopen_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:reopen.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "reopen.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_expired_index_retirement_before_B(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=(module.ROOT / "tools/replay_seek_state_selftest.inl").read_text()
    fixture=fixture.replace('../HorseMod/',str(module.ROOT / 'HorseMod')+'/')
    (tmp_path / 'fixture.inl').write_text(fixture)
    (tmp_path / 'main.cpp').write_text('#define NOMINMAX\n#include <Windows.h>\n#include <algorithm>\n#include <cstdio>\n#include <iostream>\n#include <vector>\n#include "fixture.inl"\nint main(){return ReplaySeekStateTest::expired_index_retirement_contract(false) && ReplaySeekStateTest::expired_index_retirement_contract(true) && ReplaySeekStateTest::expired_index_retirement_contract(false,true) && ReplaySeekStateTest::expired_index_retirement_contract(true,true)?0:1;}\n')
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "HorseMod/horselib"}" /I"{module.ROOT / "tools"}" /I"{module.BUILD / "HorseMod/generated"}" main.cpp /Fe:retirement.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / 'retirement.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


def test_indexed_controls_submit_initial_different_target():
    from deterministic_qualification.replay_controls import drive_indexed_controls
    report={"run_id":"entry","index_seek_target":6417,"index_seek_continuation":240}
    clicks=[]
    def click(pid,hwnd,x,y):
        clicks.append((x,y))
        return {"method":"Windows SendInput","button_released":True,"pid":pid,"hwnd":hwnd}
    raw="[GameImGui] DX11 overlay initialised \nindexed numeric entry waiting run_id=entry origin=11148 target=6417\n"
    names=["number_open","number_clear","6","4","1","7","number_submit"]
    raw+=''.join(f"replay button name={name} hwnd=10 screen_x={i+1} screen_y=2 tick=11148\n" for i,name in enumerate(names))
    for _ in range(9):drive_indexed_controls(report,7,raw,click,lambda pid:{"pid":pid,"hwnd":10})
    assert len(clicks)==7
    assert ''.join(x['digit'] for x in report['indexed_ui']['entry']['digits'])=='6417'
    assert report['indexed_ui']['entry']['origin']==11148


@pytest.mark.native_contract
def test_trace_creation_uses_exact_protected_begin_play_contract(tmp_path):
    import re
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT / 'HorseMod/horselib/deterministic'
    source=(root/'Sc6ReplayTraceRetirement.inl').read_text()
    helper=source[source.index('    static bool EmptyEvent('):source.index('    template<class Accept>')]
    creation=(root/'Sc6ReplayTraceFreshChild.inl').read_text()
    call=re.search(r'EmptyEvent\(actor->actor.object,L"ReceiveBeginPlay",[^\n]+?\)',creation).group(0)
    code=r'''
#include <string>
#include <cassert>
struct Event {
 std::wstring name=L"Function /Script/Engine.Actor:ReceiveBeginPlay";
 unsigned flags=0x08080800;int bytes{};
 const auto& GetFullName(){return name;}unsigned GetFunctionFlags(){return flags;}
 struct Script {int bytes;int Num(){return bytes;}};Script GetScript(){return {bytes};}
};
struct Object {Event event;bool present=true;Event* GetFunctionByNameInChain(const wchar_t*){return present?&event:nullptr;}};
struct State {struct {Object* object;}actor;};
'''+helper+'\nstatic bool Creation(State* actor){return '+call+';}\n'+r'''
int main(){
 Object object;State actor{{&object}};
 assert(Creation(&actor));
 object.event.flags=0x08020800;assert(!Creation(&actor));
 object.event.flags=0x08080801;assert(!Creation(&actor));
 object.event.flags=0x08080800;object.event.bytes=1;assert(!Creation(&actor));
 object.event.bytes=0;object.event.name=L"Function /Game/CustomActor:ReceiveBeginPlay";assert(!Creation(&actor));
 object.event.name=L"Function /Script/Engine.Actor:ReceiveBeginPlay";object.present=false;assert(!Creation(&actor));
 object.present=true;object.event.name=L"Function /Script/Engine.Actor:ReceiveEndPlay";object.event.flags=0x08020800;
 assert(EmptyEvent(&object,L"ReceiveEndPlay",L"Function /Script/Engine.Actor:ReceiveEndPlay"));
 object.event.flags=0x08080800;
 assert(!EmptyEvent(&object,L"ReceiveEndPlay",L"Function /Script/Engine.Actor:ReceiveEndPlay"));
}
'''
    (tmp_path/'creation.cpp').write_text(code)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc creation.cpp /Fe:creation.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'creation.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_original_viewport_copy_and_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT / 'HorseMod/horselib/GameImGui'
    source=(root / 'PresentHook.hpp').read_text()
    methods=(root / 'ReplayNativeViewport.inl').read_text()
    methods+=source[source.index('    inline bool PresentHook::retire_replay_surface()'):source.index('    inline bool PresentHook::release_replay_surface(')]
    (tmp_path / 'replay_native_viewport_methods.inl').write_text(methods)
    fixture=module.ROOT / 'tools/replay_native_viewport_selftest.cpp'
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" user32.lib d3d11.lib dxgi.lib /Fe:viewport.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / 'viewport.exe')],capture_output=True,text=True,timeout=15)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_replay_output_window_interaction(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=module.ROOT / 'tools/replay_output_window_selftest.cpp'
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "HorseMod/horselib"}" "{fixture}" user32.lib /Fe:window.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / 'window.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr



@pytest.mark.native_contract
def test_production_menu_exit_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.SessionExit.inl').read_text()
    (tmp_path / 'replay_menu_exit_methods.inl').write_text(source[source.index('bool Sc6ReplayHost::CanRequestMenuExit()'):source.index('// ReplayBattleScene.OnRequestToStop')])
    fixture=module.ROOT / 'tools/replay_menu_exit_selftest.cpp'
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:menu-exit.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path / 'menu-exit.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_indexed_menu_exit_driver_and_receipts():
    import copy
    from deterministic_qualification.replay_controls import drive_indexed_controls,validate_indexed_menu_exit
    report={'run_id':'exit','indexed_control_protocol':'retained_menu_exit_v1'}
    clicks=[]
    def click(pid,hwnd,x,y):
        clicks.append((pid,hwnd,x,y))
        return {'method':'Windows SendInput','pid':pid,'hwnd':hwnd,'window_class':'HorseModReplayOutputUi','foreground_verified':True,'button_released':True}
    raw='[GameImGui] DX11 overlay initialised \nreplay button name=exit hwnd=9 screen_x=10 screen_y=20 tick=11148\nprivate replay output displayed tick=11148 hwnd=9 native_target_untouched=true\n'
    drive_indexed_controls(report,7,raw,click,lambda pid:{'pid':pid,'hwnd':8});assert not clicks
    raw+='native menu exit waiting run_id=exit B=11148 no_transaction=true observer_request=false\n'
    for _ in range(3):drive_indexed_controls(report,7,raw,click,lambda pid:None)
    assert clicks==[(7,9,10,20)]
    raw+='native replay exit deferred tick=11148 admitted=true original_invocations=0\nreplay menu exit submitted tick=11148 accepted=true B_transaction=false native_stop_guarded=true\nnative replay exit cleanup completed requested_tick=11148\nnative session exit completed run_id=exit\n'
    assert validate_indexed_menu_exit(report,raw)['result']=='pass'
    from deterministic_qualification.replay_controls import validate_indexed_controls
    assert validate_indexed_controls(report,raw)['result']=='pass'
    with pytest.raises(RuntimeError):validate_indexed_controls(report,raw.replace('admitted=true','admitted=false'))
    for key,value in [('pid',8),('hwnd',10),('button_released',False),('foreground_verified',False),('window_class','UnrealWindow')]:
        bad=copy.deepcopy(report);bad['indexed_ui']['exit_input'][key]=value
        with pytest.raises(RuntimeError):validate_indexed_menu_exit(bad,raw)
    for bad in [raw.replace('admitted=true','admitted=false'),raw.replace('native_stop_guarded=true','native_stop_guarded=false'),raw.replace('native session exit completed run_id=exit','')]:
        with pytest.raises(RuntimeError):validate_indexed_menu_exit(report,bad)

@pytest.mark.native_contract
def test_production_capture_history_reservation(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.Capture.inl").read_text()
    method=source[source.index("bool Sc6ReplayParticleCopy::SealCapture"):source.index("bool Sc6ReplayParticleCopy::RetainLoadedCapture")]
    sharing=source[source.index("std::size_t Sc6ReplayParticleCopy::reopen_shared_bytes"):source.index("std::size_t Sc6ReplayParticleCopy::retained_capture_bytes")]
    (tmp_path / "replay_capture_seal_method.inl").write_text(sharing+method)
    fixture=module.ROOT / "tools/replay_capture_reservation_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod/horselib"}" "{fixture}" /Fe:seal.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "seal.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_index_owner_health_and_busy_admission(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=(module.ROOT / "tools/replay_seek_state_selftest.inl").read_text()
    fixture=fixture.replace('../HorseMod/',str(module.ROOT / 'HorseMod')+'/')
    (tmp_path / 'fixture.inl').write_text(fixture)
    (tmp_path / 'main.cpp').write_text('#define NOMINMAX\n#include <Windows.h>\n#include <algorithm>\n#include <cstdio>\n#include <iostream>\n#include <vector>\n#include "fixture.inl"\nint main(){bool busy=ReplaySeekStateTest::indexing_owner_health_contract(true);bool indexing=ReplaySeekStateTest::indexing_owner_health_contract(false);return busy && indexing && ReplaySeekStateTest::indexing_owner_schedule_contract() && ReplaySeekStateTest::indexing_retired_selection_contract()?0:1;}\n')
    batch=tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "HorseMod/horselib"}" /I"{module.ROOT / "tools"}" /I"{module.BUILD / "HorseMod/generated"}" main.cpp /Fe:retirement.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / 'retirement.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr



@pytest.mark.native_contract
def test_production_index_health_application_boundary(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture=(module.ROOT / "tools/replay_seek_state_selftest.inl").read_text().replace('../HorseMod/',str(module.ROOT / 'HorseMod')+'/')
    (tmp_path/'fixture.inl').write_text(fixture)
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Index.inl').read_text()
    start=source.index('void Sc6ReplayHost::FinishIndexAtApplicationBoundary()')
    end=source.index('    const auto state = index_.witness();',start)
    (tmp_path/'boundary.inl').write_text('namespace ReplaySeekStateTest {\n'+source[start:end]+'}\n}\n')
    (tmp_path/'main.cpp').write_text(r'''#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <vector>
#include "fixture.inl"
#include "boundary.inl"
int main(){
 using namespace ReplaySeekStateTest;
 using H=ReplaySeekStateTest::Sc6ReplayHost;
 for(int mode=0;mode<8;++mode){
  H h;ReplayTickIndex::Entry row{};if(!h.index_.Begin(row,16384).ok())return 1;
  auto image=std::make_shared<H::Checkpoint>();image->execution.tick=100;
  h.retained_checkpoints_.push_back(image);h.checkpoint_pins_.push_back(image);
  h.simulation_->state.tick=170;
  if(mode==1)image->vfx.admission_busy=true;
  if(mode==2)h.surface_event_=true;
  if(mode==3)h.seek_retirement_pending_=true;
  if(mode==4)h.seek_retirement_failed_=true;
  if(mode==5)h.cancel_index_on_service=true;
  if(mode==6)h.checkpoint_restoring_=true;
  if(mode==7)h.particle_command_pending_=true;
  h.FinishIndexAtApplicationBoundary();
  if(image->vfx.checks!=(mode<2?1u:0u))return 10+mode;
  if(h.index_selections!=((mode==3 || mode==4)?0u:1u))return 20+mode;
  if(h.checkpoint_pins_.size()!=1 || h.publications || h.historical_restore_)return 30+mode;
  if(mode==1 && h.health_deferrals!=1)return 41;
  if(mode==4 && h.index_.witness().phase!=ReplayTickIndex::Phase::Failed)return 44;
  if(mode==5 && h.index_.witness().phase!=ReplayTickIndex::Phase::Cancelled)return 45;
 }
 return 0;
}
''')
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "HorseMod/horselib"}" /I"{module.ROOT / "tools"}" /I"{module.BUILD / "HorseMod/generated"}" main.cpp /Fe:boundary.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'boundary.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr+str(ran.returncode)


def test_index_checkpoint_early_invalidation_evidence():
    from replay_test import validate_index_checkpoint_retention
    rows=[]
    for ordinal,tick in enumerate((170,2511,6415),1):
        kind=('initial','next-round','later-middle')[ordinal-1]
        rows.extend((f'{kind} index checkpoint selected origin={tick-1} target={tick} native_phase_tick=6 placement_owner=host',
            f'host index checkpoint armed run_id=c origin={tick-1} target={tick}',
            f'host index checkpoint adopted run_id=c tick={tick} capture_retention_and_retirement=host observer=read_only',
            f'index checkpoint owned tick={tick} retained={ordinal} owned_bytes=406607944',
            f'index checkpoint retained run_id=c tick={tick} entries={tick+1} elapsed_us=70000 owned_bytes=406607944 callbacks_unchanged=true application_idle=true',
            f'index checkpoint capture retired run_id=c tick={tick} immutable_owner_retained=true',
            f'host index checkpoint completed run_id=c tick={tick} pending=false cancelled=false'))
    health='index checkpoint ownership sample checkpoint=6415 tick=6500 phase=3 last_valid=6470 first_invalid=6500 valid_samples=2 deferred_samples=0 failure=7 retained=true restorability_unproven=true'
    retirement='invalid index checkpoint retirement tick=6415 observed_tick=6500 indexing=true native_publication=false'
    rows.extend((health,
        'index checkpoint release witness tick=6415 current_tick=6500 vfx_owners_live=false target_shape_supported=true owned_bytes=406607944',
        retirement,'seek checkpoint retired tick=6500 application_idle=true','index complete run_id=c',
        'indexed seek begin run_id=c origin=11148 target=6417 supported_index=true host_checkpoint=true',
        'indexed checkpoint selected run_id=c tick=2511 extra=6415 automatic=true',
        'indexed checkpoint fallback run_id=c rejected=6415 selected=2511 reason=retained_owner_lifetime'))
    for tick in (170,2511):rows.append(f'index checkpoint release witness tick={tick} current_tick=6537 vfx_owners_live=false target_shape_supported=true owned_bytes=406607944')
    rows.extend(('seek checkpoint retired tick=6537 application_idle=true','index released run_id=c bytes=0 hooks_removed=true'))
    raw='\n'.join(rows)
    result=validate_index_checkpoint_retention(raw,'c',2504)
    observed=result['additional_checkpoint']['checkpoints'][-1]['ownership_invalidation']
    assert observed['last_valid_tick']==6470 and observed['first_invalid_tick']==6500
    assert observed['exact_invalidating_instruction_known'] is False
    first_check_invalid=raw.replace('last_valid=6470','last_valid=0').replace('valid_samples=2','valid_samples=0')
    first_observed=validate_index_checkpoint_retention(first_check_invalid,'c',2504)['additional_checkpoint']['checkpoints'][-1]['ownership_invalidation']
    assert first_observed['last_valid_tick'] is None and first_observed['first_invalid_tick']==6500
    for bad in (raw.replace(health,''),raw.replace(retirement,''),raw.replace('phase=3','phase=2'),
                raw.replace('last_valid=6470','last_valid=6501'),raw.replace('first_invalid=6500','first_invalid=6501'),
                raw.replace('failure=7','failure=0'),raw.replace('seek checkpoint retired tick=6500 application_idle=true','')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)


@pytest.mark.parametrize('sequence',(1,2))
def test_index_sequence_case_boundaries_fail_closed(sequence):
    from deterministic_qualification.indexed_sequence import TARGETS,sequence_cases
    rows=[];origin=11148
    for i,target in enumerate(TARGETS[sequence]):
        checkpoint=170 if origin<2511 or target<2511 else 2511
        rows.extend((f'indexed sequence case run_id=c sequence={sequence} case={i} target={target} B={origin} callbacks={100+i*100} interval={origin} previous_release_completed={str(i>0).lower()}',
            f'indexed seek begin run_id=c origin={origin} target={target} supported_index=true host_checkpoint=true callbacks={100+i*100} interval={origin}',
            f'indexed checkpoint selected run_id=c tick={checkpoint} extra=6415 automatic=true',
            f'indexed seek continuation run_id=c first={target+1} last={target+120} ticks=120'))
        origin=target+121
    rows.append(f'indexed sequence completed run_id=c sequence={sequence} cases={len(TARGETS[sequence])} continuation_ticks_each=120')
    raw='\n'.join(rows)
    assert tuple(c['target'] for c in sequence_cases(raw,'c'))==TARGETS[sequence]
    for i in range(len(rows)):
        with pytest.raises(RuntimeError):sequence_cases('\n'.join(rows[:i]+rows[i+1:]),'c')
    for bad in (raw+rows[-1],raw.replace('previous_release_completed=true','previous_release_completed=false'),
                raw.replace('ticks=120','ticks=119'),raw.replace('case=1','case=0'),raw.replace('B=329','B=330')):
        if bad!=raw:
            with pytest.raises(RuntimeError):sequence_cases(bad,'c')


@pytest.mark.native_contract
def test_production_index_sequence_release_boundary(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('    bool ObserveIndexedSequenceBoundary(')
    end=source.index('    bool BeginIndexedSeek(',start)
    method=source[start:end].replace('bool ObserveIndexedSequenceBoundary(', 'bool ReplayQualificationMod::ObserveIndexedSequenceBoundary(',1)
    (tmp_path/'sequence_boundary_method.inl').write_text(method)
    fixture=module.ROOT/'tools/replay_index_sequence_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod/horselib"}" /I"{module.ROOT / "tools"}" /I"{module.BUILD / "HorseMod/generated"}" "{fixture}" /Fe:sequence.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'sequence.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr+str(ran.returncode)


def test_index_sequence_same_process_hud_scope(tmp_path):
    from replay_test import validate_indexed_same_process_hud
    from deterministic_qualification.indexed_sequence import TARGETS
    from deterministic_qualification.artifacts import sha256_file
    targets=TARGETS[1];rows=[]
    def hud(tick):return f'[ReplayQualification] HUD state tick={tick} p1=1 p2=1 combo1=0 combo2=0 timer=9 damage=0 types=0 pool=0 active=0 type_pool=0 type_active=0 type_players=none read_only=true'
    for target in set(targets):rows.append(f'[ReplayQualification] HUD observation window target={target} first={target-16} last={target+120} read_only=true')
    for tick in sorted({tick for target in targets for tick in range(target+1,target+121)}):rows.append(hud(tick))
    origin=11148
    for i,target in enumerate(targets):
        checkpoint=170 if origin<2511 or target<2511 else 2511
        rows.extend((f'indexed sequence case run_id=c sequence=1 case={i} target={target} B={origin} callbacks={100+i*100} interval={origin} previous_release_completed={str(i>0).lower()}',
            f'indexed seek begin run_id=c origin={origin} target={target} supported_index=true host_checkpoint=true callbacks={100+i*100} interval={origin}',
            f'indexed checkpoint selected run_id=c tick={checkpoint} extra=6415 automatic=true',
            f'indexed seek resumed run_id=c tick={target} target_tails_complete=true'))
        rows.extend(hud(tick) for tick in range(target+1,target+121))
        rows.append(f'indexed seek continuation run_id=c first={target+1} last={target+120} ticks=120');origin=target+121
    rows.append(f'indexed sequence completed run_id=c sequence=1 cases={len(targets)} continuation_ticks_each=120')
    raw=tmp_path/'raw.log';raw.write_text('\n'.join(rows))
    report=dict(run_id='c',index_sequence='early',raw_log=dict(path=str(raw),sha256=sha256_file(raw)))
    assert len(validate_indexed_same_process_hud(report)['cases'])==5
    rows[-4]=rows[-4].replace('p1=1','p1=0');raw.write_text('\n'.join(rows));report['raw_log']['sha256']=sha256_file(raw)
    with pytest.raises(RuntimeError,match='HUD first mismatch'):validate_indexed_same_process_hud(report)


def test_index_sequence_physical_rewinds_and_bridges():
    from deterministic_qualification.indexed_sequence import TARGETS,boundary_streams
    keys="round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves source_active manager_phase move_state round_state published_count published_pairs".split()
    common=" ".join(k+"=0" for k in keys);lines=['boundaries started run_id=c native_frame=0'];ordinal=0
    def row(tick):
        nonlocal ordinal
        ordinal+=1;lines.append(f'[ReplayQualification] boundary ordinal={ordinal} phase=actor_tail sample_version=2 frame={tick} {common}')
    row(11148)
    lines.extend(('index supported range run_id=c source_tick=11147 last_tick=11148 tail_ticks=1 all_retained_ticks_indexed=true unsupported_native_tail=true',
        'retained source stop complete source_tick=11147 last_tick=11148 entries=11149 intervals=11148 empty=0 multi=0 bytes=4096 native_finish=false application_idle=true bindings_valid=true unsupported_native_tail=true',
        'index supported complete run_id=c entries=11149 bytes=4096 intervals=11148 empty=0 multi=0 native_finish=false unsupported_native_tail=true',
        'index closure run_id=c replay_tick=11147 native_tick=11148 callbacks_retained=true'))
    origin=11148
    for i,target in enumerate(TARGETS[1]):
        checkpoint=170 if origin<2511 or target<2511 else 2511
        lines.extend((f'indexed sequence case run_id=c sequence=1 case={i} target={target} B={origin} callbacks={ordinal} interval={origin} previous_release_completed={str(i>0).lower()}',
            f'indexed seek begin run_id=c origin={origin} target={target} supported_index=true host_checkpoint=true callbacks={ordinal} interval={origin}',
            f'indexed checkpoint selected run_id=c tick={checkpoint} extra=6415 automatic=true',
            f'historical combat execution admitted run_id=c from_tick={origin} to_tick={checkpoint} B_retained=true'))
        row(checkpoint)
        row(target+120)
        lines.append(f'indexed seek continuation run_id=c first={target+1} last={target+120} ticks=120')
        if i+1<len(TARGETS[1]):row(target+121)
        origin=target+121
    lines.extend(('indexed sequence completed run_id=c sequence=1 cases=5 continuation_ticks_each=120',f'boundaries completed run_id=c observations={ordinal} detached=true'))
    text='\n'.join(lines);all_rows,prefix,_,metadata=boundary_streams(text,'c')
    assert len(prefix)==1 and len(all_rows)==15 and len(metadata['cases'])==5
    # This tests structural boundaries only; independent contiguous callback comparison is separate.
    for bad in (text.replace('callbacks=4','callbacks=5'),text.replace('from_tick=329','from_tick=330'),
                text.replace('frame=329','frame=330'),text.replace('observations=15','observations=14')):
        assert bad!=text
        with pytest.raises(RuntimeError):boundary_streams(bad,'c')


@pytest.mark.native_contract
def test_production_trace_render_recovery_poisoned_retry(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp").read_text()
    journal_start=source.index("    struct RenderRecovery {")
    journal_end=source.index("    Status RetireHiddenRenderingForUndo(",journal_start)
    (tmp_path / "trace_render_journal.inl").write_text(source[journal_start:journal_end])
    start=source.index("    Status RetireHiddenRenderingForUndo(")
    method=source[start:source.index("\nprivate:",start)]
    # The pre-fix implementation has no journal parameter. Add only an unused
    # fixture argument so the identical production body can demonstrate retry.
    if "RenderRecovery& journal" not in method:
        method=method.replace("const Sc6ReplayTraceState& target) const", "const Sc6ReplayTraceState& target,RenderRecovery& journal) const",1)
    (tmp_path / "trace_render_method.inl").write_text(method)
    fixture=module.ROOT / "tools/replay_trace_render_recovery_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:trace-render.exe /Fo:fixture.obj\n')
    result=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    result=subprocess.run([str(tmp_path / "trace-render.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


@pytest.mark.parametrize('original', [329,5695,6538])
def test_trace_render_recovery_admission(original):
    from deterministic_qualification.replay_fidelity import trace_render_recovery_case
    from deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=original,host_seek_target=2510,
        host_seek=True,historical_cancel="after",seek_advance_failure=True,seek_settlement_failure=True,
        seek_drained_cancel=True,checkpoint_pair=False)
    assert trace_render_recovery_case(report) and historical_capture_interval_supported(report)
    for key in ("seek_advance_failure","seek_settlement_failure","seek_drained_cancel","host_seek"):
        invalid=dict(report,**{key:False});assert not trace_render_recovery_case(invalid)
        assert not historical_capture_interval_supported(invalid)
    for change in [dict(historical_cancel="before"),dict(host_seek_target=301),dict(corrected_inputs=True),
                   dict(source_revision=True),dict(host_seek_repeat=True),dict(seek_observer_failure=True)]:
        assert not trace_render_recovery_case(dict(report,**change))


@pytest.mark.workflow
@pytest.mark.parametrize('original', [329,5695,6538])
def test_trace_render_recovery_cli_reaches_build(tmp_path,monkeypatch,original):
    import replay_test as module
    monkeypatch.setattr(module.sys,"argv",["replay_test.py","combat-restore","--host-seek",
        "--combat-case","after","--combat-anchor-tick","170","--combat-advanced-tick",str(original),
        "--host-seek-target","2510","--seek-drained-cancel"])
    monkeypatch.setattr(module,"OUTPUT",tmp_path);entered=[]
    def boundary(*_):
        entered.append(True);raise RuntimeError("fixture stopped before build or live launch")
    monkeypatch.setattr(module,"ensure_build",boundary)
    assert module.main()==1 and entered==[True]


@pytest.mark.workflow
def test_late_trace_recovery_observation_budget_reaches_B_and_suffix(tmp_path,monkeypatch):
    import replay_test as module
    made=[];original_args=module.control_args
    def control_args(*args,**kwargs):
        result=original_args(*args,**kwargs);made.append(result);return result
    monkeypatch.setattr(module,'control_args',control_args)
    monkeypatch.setattr(module.sys,'argv',['replay_test.py','combat-restore','--host-seek',
        '--combat-case','after','--combat-anchor-tick','170','--combat-advanced-tick','5695',
        '--host-seek-target','2510','--seek-drained-cancel'])
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module,'ensure_build',lambda *_:{})
    seen=[]
    def capture(args):
        seen.append(args)
        raise RuntimeError('fixture stops before native launch')
    monkeypatch.setattr(module,'capture',capture)
    assert module.main()==1 and len(seen)==1
    assert seen[0].historical_advanced_tick==5695
    assert seen[0].watch_frames>=5695+(2510-170)+120
    assert len(made)==2 and all(getattr(args,'observation_target',0)==5695 for args in made)


def test_trace_render_reconstruction_requires_completed_native_work():
    from replay_test import validate_trace_render_reconstruction
    native="trace visible B reconstruction count=2 source_animation_unchanged=true B_retained=true render_drain_required=true"
    publish="stage render recovery state=PublicationPending B_retained=true native_destinations_rebound=true"
    complete="stage render recovery state=Complete B_retained=true GPU_completed=true"
    assert validate_trace_render_reconstruction("\n".join((native,publish,complete)))["visible_meshes_reconstructed"]==2
    for invalid in (native,native+"\n"+complete,"\n".join((complete,native,publish)),"\n".join((native,publish,complete)).replace("count=2","count=0")):
        with pytest.raises(RuntimeError):validate_trace_render_reconstruction(invalid)


def test_private_trace_retention_keeps_reconstruction_coverage_separate():
    from replay_test import validate_trace_render_reconstruction
    raw="\n".join((*(f"trace capture roots=2 states={n} values=3 bindings=4 bytes=55 success=true" for n in (109,110,109)),
        "trace visible B reconstruction count=0 source_animation_unchanged=true B_retained=true render_drain_required=true",
        "stage render recovery state=PublicationPending B_retained=true native_destinations_rebound=true",
        "stage render recovery state=Complete B_retained=true GPU_completed=true"))
    result=validate_trace_render_reconstruction(raw,5695)
    assert result['private_B_children_retained']==1 and not result['reconstruction_exercised'] and result['GPU_completed']
    for broken in (raw.replace('states=110','states=109'),raw.replace('GPU_completed=true','GPU_completed=false'),raw.replace('native_destinations_rebound=true','native_destinations_rebound=false')):
        with pytest.raises(RuntimeError):validate_trace_render_reconstruction(broken,5695)
    with pytest.raises(RuntimeError):validate_trace_render_reconstruction(raw,329)


@pytest.mark.native_contract
def test_production_trace_render_binding_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayTraceState.hpp").read_text()
    a=source.index("    static bool Equal(");equal=source[a:source.index("    static bool Writable(",a)]
    a=source.index("    bool RetainedRenderBinding(");method=source[a:source.index("    // Recovery only:",a)]
    (tmp_path / "trace_render_binding_methods.inl").write_text(equal+method)
    fixture=module.ROOT / "tools/replay_trace_render_binding_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:trace-binding.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path / "trace-binding.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


@pytest.mark.parametrize("original",[329,6538])
def test_trace_only_recovery_does_not_require_unrelated_particle_births(original):
    import replay_test
    # Bounded exact producer excerpt from replay-066b09d995604f599d90d5fa108ca051.
    raw='[2026-09-13 20:12:03.7155143] [HorseMod] seek ownership state=BRetained B_tick=329 target=2510 current_tick=329 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:03.7377132] [HorseMod] seek ownership state=APublished B_tick=329 target=2510 current_tick=170 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:03.7460132] [HorseMod] seek ownership state=ExecutionActive B_tick=329 target=2510 current_tick=170 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:10.3771513] [HorseMod] seek ownership state=CSettled B_tick=329 target=2510 current_tick=2510 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:11.3785054] [HorseMod] seek ownership state=CompletingTargetTails B_tick=329 target=2510 current_tick=2510 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:11.4117342] [HorseMod] seek render drain completed tick=2510 recovery=false gpu_complete=true render_settled=false\n[2026-09-13 20:12:11.4624323] [HorseMod] seek drained cancellation boundary tick=2510 gpu_complete=true C_captured=true render_settled=false cpu_settled=0 B_retained=true\n[2026-09-13 20:12:11.4624430] [HorseMod] seek ownership state=FailedRecoverable B_tick=329 target=2510 current_tick=2510 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:11.4629937] [HorseMod] seek ownership state=RecoveryQuiescing B_tick=329 target=2510 current_tick=2510 completed_tail_tick=0 B_retained=true commit_decided=false executed_ticks=0 executed_intervals=0\n[2026-09-13 20:12:11.4654307] [HorseMod] seek drained recovery routed tick=2510 gpu_complete=true render_settled=false cpu_settled=0 B_retained=true fresh_retirement_drain_required=true\n[2026-09-13 20:12:11.4901696] [HorseMod] seek C-only retirement owners=0 completed=0 B_retained=true native_render_drain_required=true\n[2026-09-13 20:12:11.4953955] [HorseMod] seek render drain completed tick=2510 recovery=true gpu_complete=true render_settled=false\n[2026-09-13 20:12:11.5363488] [HorseMod] seek ownership state=Recovering B_tick=329 target=2510 current_tick=2510 completed_tail_tick=2510 B_retained=true commit_decided=false executed_ticks=2340 executed_intervals=2340\n[2026-09-13 20:12:11.5372926] [HorseMod] stage render recovery state=CpuPending B_retained=true native_destinations_deferred=true\n[2026-09-13 20:12:11.5473078] [HorseMod] trace visible B reconstruction count=2 source_animation_unchanged=true B_retained=true render_drain_required=true\n[2026-09-13 20:12:11.5537145] [HorseMod] stage render recovery state=PublicationPending B_retained=true native_destinations_rebound=true\n[2026-09-13 20:12:11.5565935] [HorseMod] stage render recovery state=Complete B_retained=true GPU_completed=true\n[2026-09-13 20:12:11.5604062] [HorseMod] seek ownership state=Recovered B_tick=329 target=2510 current_tick=329 completed_tail_tick=2510 B_retained=false commit_decided=false executed_ticks=2340 executed_intervals=2340\n'
    raw=raw.replace("B_tick=329",f"B_tick={original}").replace("current_tick=329",f"current_tick={original}")
    if original==6538:raw=raw.replace("count=2 source_animation","count=0 source_animation")
    result=replay_test.validate_seek_recovery_ownership(raw,170,original,2510,2510,True,True)
    assert result["C_only_particles"]==0 and result["B_recovered_tick"]==original
    for bad in (raw.replace("source_animation_unchanged=true", "source_animation_unchanged=false"),
                raw.replace("owners=0 completed=0","owners=0 completed=1"),
                raw.replace("GPU_completed=true","GPU_completed=false"),
                raw.replace("source_animation_unchanged=true","source_animation_unchanged=false")):
        with pytest.raises(RuntimeError):replay_test.validate_seek_recovery_ownership(bad,170,original,2510,2510,True,True)

@pytest.mark.native_contract
def test_production_sequence_hud_observation_capacity(tmp_path):
    import re, subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / 'tools/replay_qualification_mod/ReplayBoundaryObserver.hpp').read_text()
    guards=[]
    for field in ('hud_state_rows_', 'hud_clock_rows_'):
        guards.append(re.search(r'if\((\+\+self\.'+field+r'.*?)\)\s*\{self\.task_failed_',source).group(1))
    code='''#include <cstdio>
struct Observer {bool index_sequence_{};unsigned hud_state_rows_{},hud_clock_rows_{},hud_target_{};};
'''
    for name,condition in zip(('State','Clock'),guards):
        code+=f'bool {name}(Observer& self){{return !({condition});}}\n'
    code+='''int main(){
for(unsigned target:{0u,209u,210u,246u,390u,816u,817u,2510u}) for(bool sequence:{false,true}) {
 Observer state{sequence,0,0,target},clock{sequence,0,0,target};
 const bool expanded=sequence || (target>=210 && target<=816);
 const unsigned state_limit=expanded?8192:1024,clock_limit=expanded?16384:2048;
 for(unsigned i=0;i<state_limit;++i)if(!State(state)){std::printf("state rejected sequence=%d row=%u\\n",sequence,i+1);return 1;}
 if(State(state)||State(state))return 2;
 for(unsigned i=0;i<clock_limit;++i)if(!Clock(clock))return 3;
 if(Clock(clock)||Clock(clock))return 4;
}return 0;}
'''
    code='#include <initializer_list>\n'+code
    (tmp_path/'hud-limit.cpp').write_text(code)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc hud-limit.cpp /Fe:hud-limit.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path/'hud-limit.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr

def test_held_capture_uses_window_rendering_not_desktop(tmp_path):
    import os, win32gui, win32api, win32con
    from PIL import Image
    from deterministic_qualification.replay_controls import capture_indexed_control_hold
    brush=win32gui.CreateSolidBrush(win32api.RGB(231,17,93))
    def procedure(hwnd,message,wparam,lparam):
        if message in (win32con.WM_PRINT,win32con.WM_PRINTCLIENT):
            win32gui.FillRect(wparam,(0,0,320,200),brush);return 0
        if message==win32con.WM_PAINT:
            dc,paint=win32gui.BeginPaint(hwnd);win32gui.FillRect(dc,(0,0,320,200),brush);win32gui.EndPaint(hwnd,paint);return 0
        return win32gui.DefWindowProc(hwnd,message,wparam,lparam)
    cls=win32gui.WNDCLASS();cls.lpfnWndProc=procedure
    cls.hInstance=win32api.GetModuleHandle(None);cls.lpszClassName='ReplayCaptureRegression'+str(os.getpid())
    atom=win32gui.RegisterClass(cls)
    hwnd=win32gui.CreateWindowEx(0,atom,'owned hidden replay fixture',win32con.WS_POPUP,-10000,-10000,320,200,0,0,cls.hInstance,None)
    win32gui.ShowWindow(hwnd,win32con.SW_SHOWNOACTIVATE);win32gui.UpdateWindow(hwnd)
    try:
        report=dict(run_id='capture-fixture',indexed_ui=dict(tick=208))
        capture_indexed_control_hold(report,tmp_path/'run.json',os.getpid(),f'private replay output displayed tick=208 hwnd={hwnd} native_target_untouched=true')
        image=Image.open(report['indexed_ui']['images']['held']['path'])
        assert image.getpixel((160,100))==(231,17,93),'capture sampled desktop instead of the owned window rendering'
    finally:
        win32gui.DestroyWindow(hwnd);win32gui.UnregisterClass(atom,cls.hInstance);win32gui.DeleteObject(brush)

def test_sequence_final_case_includes_application_tail():
    from deterministic_qualification.indexed_sequence import TARGETS,sequence_cases
    rows=[];origin=11148
    for i,target in enumerate(TARGETS[1]):
        rows.extend((f'indexed sequence case run_id=c sequence=1 case={i} target={target} B={origin} callbacks={100+i*100} interval={origin} previous_release_completed={str(i>0).lower()}',
            f'indexed seek begin run_id=c origin={origin} target={target} supported_index=true host_checkpoint=true callbacks={100+i*100} interval={origin}',
            f'indexed checkpoint selected run_id=c tick=170 extra=6415 automatic=true',
            f'indexed seek continuation run_id=c first={target+1} last={target+120} ticks=120'))
        origin=target+121
    rows+=['indexed sequence completed run_id=c sequence=1 cases=5 continuation_ticks_each=120',
           '[ReplayQualification] HUD state tick=328 native_application_tail=true',
           'native presentation completed run_id=c detached=true']
    raw='\n'.join(rows);last=sequence_cases(raw,'c')[-1]
    assert 'HUD state tick=328' in raw[last['start_offset']:last['end_offset']]

@pytest.mark.native_contract
def test_production_replay_memory_default_one_gib(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source='#include "Schema.hpp"\nint main(){using namespace Horse::Deterministic;return Schema::replay_timeline_memory_limit==1024ull*1024*1024 ? 0:1;}\n'
    (tmp_path/'budget.cpp').write_text(source)
    batch=tmp_path/'compile.cmd';include=module.ROOT/'HorseMod/horselib/deterministic'
    generated=module.BUILD/'HorseMod/generated'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" /I"{generated}" budget.cpp /Fe:budget.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path/'budget.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,'production replay default is not the accepted 1 GiB'


def test_indexed_costs_accept_new_production_budget_without_relaxing_ceiling():
    from replay_test import indexed_seek_costs
    text='indexed seek ready run_id=c target=208 elapsed_us=500000 owned_bytes=805306368 diagnostic_gpu_readbacks=false\nindexed seek resume rate run_id=c target=208 ticks=120 elapsed_us=2000000\n'
    text+='indexed seek application cost run_id=c target=208 preparation_us=100000 prefix_us=10000 engine_us=200000 tail_us=100000 frame_sync_us=10000 includes_observers=true\nindexed seek completion cost run_id=c target=208 request_to_completed_us=1100000 validation_hold_us=500000 completion_us=100000 application_idle=true pending_task=false release_completed=true diagnostic_gpu_readbacks=false\n'
    assert indexed_seek_costs(text,'c',208)['owned_bytes']==805306368
    with pytest.raises(RuntimeError):indexed_seek_costs(text.replace('805306368','1073741825'),'c',208)


@pytest.mark.native_contract
def test_production_memory_selection_preserves_active_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.cpp').read_text()
    start=source.index('bool Sc6ReplayHost::ConfigureMemoryBudget(')
    body=source[start:source.index('\nbool Sc6ReplayHost::ValidWorld',start)].strip()
    code='''#include <atomic>
#include <vector>
#include <cstddef>
namespace Schema {constexpr size_t replay_timeline_memory_limit=1ull<<30,replay_debug_memory_limit=2ull<<30;}
unsigned GetCurrentThreadId(){return 7;}
struct Simulation {struct C {unsigned tick{};} c; C continuation(){return c;}};
struct Sc6ReplayHost {
 enum class CapturePhase {Idle,Ready,Failed};struct Capture {CapturePhase phase{};} capture_operation_;
 inline static Sc6ReplayHost* active_{};
 unsigned thread_{7},depth_{};Simulation* simulation_{};
 bool historical_restore_{},restore_undo_{},captured_checkpoint_{},particle_copy_{},surface_event_{},exit_{};
 std::vector<int> retained_checkpoints_,checkpoint_pins_;
 std::atomic<bool> particle_command_pending_{};
 size_t memory_limit_{Schema::replay_timeline_memory_limit};
 bool SessionExitRequested(){return exit_;}
 static bool ConfigureMemoryBudget(size_t,bool) noexcept;
};
'''+body+'''
int main(){
 Sc6ReplayHost h;Simulation sim;h.simulation_=&sim;Sc6ReplayHost::active_=&h;
 auto reject=[&](){auto old=h.memory_limit_;return !h.ConfigureMemoryBudget(2ull<<30,true)&&h.memory_limit_==old;};
 if(!h.ConfigureMemoryBudget(1ull<<30,false)||h.ConfigureMemoryBudget(2ull<<30,false)||h.ConfigureMemoryBudget((2ull<<30)+1,true))return 1;
 if(!h.ConfigureMemoryBudget(2ull<<30,true)||h.memory_limit_!=(2ull<<30))return 2;
 for(bool* owner:{&h.historical_restore_,&h.restore_undo_,&h.captured_checkpoint_,&h.particle_copy_,&h.surface_event_,&h.exit_}){*owner=true;if(!reject())return 3;*owner=false;}
 h.retained_checkpoints_.push_back(1);if(!reject())return 4;h.retained_checkpoints_.clear();
 h.checkpoint_pins_.push_back(1);if(!reject())return 5;h.checkpoint_pins_.clear();
 h.particle_command_pending_=true;if(!reject())return 6;h.particle_command_pending_=false;
 for(auto phase:{Sc6ReplayHost::CapturePhase::Ready,Sc6ReplayHost::CapturePhase::Failed}){h.capture_operation_.phase=phase;if(!reject())return 7;}h.capture_operation_.phase=Sc6ReplayHost::CapturePhase::Idle;
 sim.c.tick=1;if(!reject())return 8;sim.c.tick=0;
 h.depth_=1;if(!reject())return 9;h.depth_=0;h.thread_=8;if(!reject())return 10;
 Sc6ReplayHost::active_=nullptr;if(h.ConfigureMemoryBudget(2ull<<30,true))return 11;
 return 0;}
'''
    (tmp_path/'selection.cpp').write_text(code)
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc selection.cpp /Fe:selection.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    assert subprocess.run([str(tmp_path/'selection.exe')],cwd=tmp_path,timeout=10).returncode==0


def test_debug_memory_admission_requires_exact_bounded_receipt():
    from replay_test import replay_memory_limit
    receipt='replay memory budget run_id=c bytes=2147483648 diagnostic=true native_tick=0'
    assert replay_memory_limit('', 'c')==1073741824
    assert replay_memory_limit(receipt,'c')==2147483648
    for bad in (receipt+receipt,receipt.replace('run_id=c','run_id=x'),receipt.replace('2147483648','2147483649'),receipt.replace('true','false'),receipt.replace('tick=0','tick=1')):
        with pytest.raises(RuntimeError):replay_memory_limit(bad,'c')


@pytest.mark.parametrize('middle_kind', ['later-middle', 'next-round'])
def test_four_checkpoint_inventory_distinguishes_retention_from_capture(middle_kind):
    from replay_test import validate_index_checkpoint_retention
    rows=[]
    for ordinal,tick in enumerate((170,2511,6415,7510),1):
        kind='initial' if ordinal==1 else middle_kind if ordinal==3 else 'next-round'
        rows += [f'{kind} index checkpoint selected origin={tick-1} target={tick} native_phase_tick=6 placement_owner=host',
            f'host index checkpoint armed run_id=c origin={tick-1} target={tick}',
            f'host index checkpoint adopted run_id=c tick={tick} capture_retention_and_retirement=host observer=read_only',
            f'index checkpoint owned tick={tick} retained={min(ordinal,3)} owned_bytes=600000000',
            f'index checkpoint retained run_id=c tick={tick} entries={tick+1} elapsed_us=70000 owned_bytes=600000000 callbacks_unchanged=true application_idle=true',
            f'index checkpoint capture retired run_id=c tick={tick} immutable_owner_retained=true',
            f'host index checkpoint completed run_id=c tick={tick} pending=false cancelled=false']
        if tick==6415:
            rows += ['index checkpoint ownership sample checkpoint=6415 tick=6445 phase=3 last_valid=6415 first_invalid=6445 valid_samples=1 deferred_samples=0 failure=7 retained=true restorability_unproven=true',
                'index checkpoint release witness tick=6415 current_tick=6445 vfx_owners_live=false target_shape_supported=true owned_bytes=600000000',
                'invalid index checkpoint retirement tick=6415 observed_tick=6445 indexing=true native_publication=false',
                'seek checkpoint retired tick=6445 application_idle=true']
    rows += ['index complete run_id=c','indexed seek begin run_id=c origin=11148 target=7512 supported_index=true host_checkpoint=true',
        'indexed checkpoint selected run_id=c tick=7510 extra=7510 automatic=true']
    for tick in (170,2511,7510):rows += [f'index checkpoint release witness tick={tick} current_tick=7633 vfx_owners_live=false target_shape_supported=true owned_bytes=600000000']
    rows += ['seek checkpoint retired tick=7633 application_idle=true','index released run_id=c bytes=0 hooks_removed=true']
    raw='\n'.join(rows)
    result=validate_index_checkpoint_retention(raw,'c',2504)['additional_checkpoint']
    assert len(result['checkpoints'])==4 and result['selected_tick']==7510
    assert result['checkpoints'][-1]['retained_count_at_capture']==3
    replacement=raw.replace('next-round index checkpoint selected origin=7509',
        'replacement index checkpoint selected origin=7509').replace(
        'host index checkpoint armed run_id=c origin=7509',
        'index replacement placement target=7510 rejected_checkpoint=6415 invalidated_at=6445 attempt=1 retirement_idle=true\n'
        'host index checkpoint armed run_id=c origin=7509')
    replacement_result=validate_index_checkpoint_retention(replacement,'c',2504)['additional_checkpoint']
    assert replacement_result['checkpoints'][-1]['placement']['replacement']['rejected_checkpoint']==6415
    for bad in (replacement.replace('retirement_idle=true','retirement_idle=false'),
                replacement.replace('invalidated_at=6445','invalidated_at=7500'),
                replacement.replace('attempt=1','attempt=3'),
                replacement.replace('invalid index checkpoint retirement tick=6415 observed_tick=6445 indexing=true native_publication=false','')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)
    skipped='\n'.join(['next-round index checkpoint selected origin=3999 target=4000 native_phase_tick=6 placement_owner=host',
        'host index checkpoint armed run_id=c origin=3999 target=4000',
        'host index checkpoint adopted run_id=c tick=4000 capture_retention_and_retirement=host observer=read_only',
        'optional index checkpoint skipped run_id=c tick=4000 capture=false retained=false callbacks_unchanged=true application_idle=true monitor_released=true',
        'optional index checkpoint unsupported tick=4000 original_anchor_retained=true',
        'host index checkpoint completed run_id=c tick=4000 pending=false cancelled=false'])+'\n'
    with_skip=raw.replace(f'{middle_kind} index checkpoint selected origin=6414',skipped+f'{middle_kind} index checkpoint selected origin=6414')
    evidence=validate_index_checkpoint_retention(with_skip,'c',2504)['additional_checkpoint']['unsupported_placements']
    assert evidence==[dict(tick=4000,reason='target_eligibility',captured=False,retained=False,resume_completed=True)]
    for bad in (with_skip.replace('monitor_released=true','monitor_released=false'),with_skip.replace('optional index checkpoint unsupported tick=4000 original_anchor_retained=true',''),with_skip.replace('host index checkpoint completed run_id=c tick=4000 pending=false cancelled=false','')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)
    natural='\n'.join([
        'restore undo captured target=7510 original=11148 gpu_complete=true before_target_preparation=true',
        'restore preparation failed participant=preparation_domain code=7 target=7510 original=11148 before_publication=true',
        'restore preparation recovery completed original=11148 observed=11148 ground_released=true render_released=true before_A_publication=true',
        'seek preparation fallback rejected=7510 selected=2511 failure=7 B_recovered=true preparation_released=true publication=false',
        'restore undo captured target=2511 original=11148 gpu_complete=true before_target_preparation=true',
        'indexed natural preparation fallback run_id=c selected=2511 attempts=1 last_failure=7 B=11148 automatic=true'])+'\n'
    natural_raw=raw.replace('indexed checkpoint selected run_id=c tick=7510 extra=7510 automatic=true',natural+'indexed checkpoint selected run_id=c tick=2511 extra=7510 automatic=true')
    natural_result=validate_index_checkpoint_retention(natural_raw,'c',2504)['additional_checkpoint']['selections'][0]
    assert natural_result['preparation_recovery']==dict(attempts=1,rejected=[7510],selected=2511,original=11148,complete_B_recovered=True)
    cpu_raw=natural_raw.replace('restore preparation failed participant=preparation_domain code=7 target=7510 original=11148 before_publication=true',
        'historical restore preflight participant=physics_markers code=7 target=7510 original=11148 bytes=440199730')
    assert validate_index_checkpoint_retention(cpu_raw,'c',2504)['additional_checkpoint']['selections'][0]==natural_result
    cpu_receipt='historical restore preflight participant=physics_markers code=7 target=7510 original=11148 bytes=440199730'
    for bad in (cpu_raw.replace(cpu_receipt,cpu_receipt.replace('code=7','code=5')),
                cpu_raw.replace(cpu_receipt,cpu_receipt.replace('original=11148','original=11149')),
                cpu_raw.replace(cpu_receipt,cpu_receipt+'\n'+cpu_receipt),
                cpu_raw.replace('ground_released=true','ground_released=false')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)
    for bad in (natural_raw.replace('ground_released=true','ground_released=false'),natural_raw.replace('attempts=1','attempts=2'),natural_raw.replace('last_failure=7','last_failure=5'),natural_raw.replace('observed=11148','observed=11149'),natural_raw.replace('before_A_publication=true','before_A_publication=false')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)
    for bad in (raw.replace('tick=7510 retained=3','tick=7510 retained=4'),raw.replace('target=7510 native_phase_tick=6','target=7510 native_phase_tick=5'),raw.replace('seek checkpoint retired tick=6445 application_idle=true','')):
        with pytest.raises(RuntimeError):validate_index_checkpoint_retention(bad,'c',2504)


@pytest.mark.native_contract
def test_rebinding_host_resets_diagnostic_budget_before_native_admission(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    production=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.cpp').read_text()
    start=production.index('bool Sc6ReplayHost::Bind(')
    body=production[start:production.index('    set_engine_body_ =',start)]+'return true;\n}\n'
    source='''#include <cstdint>
#include <cstddef>
namespace Schema {constexpr size_t replay_timeline_memory_limit=1ull<<30;}
struct Sc6ReplayExecutor {};struct UcrtRandBroker {};
struct Sc6ReplayHost {
 inline static Sc6ReplayHost* active_{};
 bool application_hook_{},session_exit_hook_{};unsigned session_exit_{};
 void* session_exit_scene_{};void* session_exit_manager_{};void* session_exit_function_{};
 size_t memory_limit_{2ull<<30};
 bool Bind(uintptr_t,void*,Sc6ReplayExecutor*,bool,UcrtRandBroker*,void*,size_t(*)(void*) noexcept) noexcept;
};
'''+body+'''
int main(){Sc6ReplayHost h;Sc6ReplayHost::active_=&h;
 if(h.Bind(1,(void*)1,nullptr,false,nullptr,nullptr,nullptr)||h.memory_limit_!=(2ull<<30))return 1;
 Sc6ReplayHost::active_=nullptr;
 if(!h.Bind(1,(void*)1,nullptr,false,nullptr,nullptr,nullptr)||h.memory_limit_!=Schema::replay_timeline_memory_limit)return 2;
 return 0;}
'''
    (tmp_path/'rebind.cpp').write_text(source)
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc rebind.cpp /Fe:rebind.exe\n')
    result=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    assert subprocess.run([str(tmp_path/'rebind.exe')],cwd=tmp_path,timeout=10).returncode==0


@pytest.mark.parametrize("tick,round_number,cross_round",[(329,0,False),(2504,1,False),(5695,2,False),(5695,2,True)])
def test_combat_native_selection_uses_absolute_ticks(tick,round_number,cross_round):
    import ast
    import inspect
    import replay_test
    # Execute the actual production selection expressions. Reference rounds
    # come from independent native observations, never a hardcoded B shortcut.
    tree=ast.parse(inspect.getsource(replay_test.compare_historical_combat))
    reference=[dict(frame=str(t),round=str(round_number+int(cross_round and t>tick+60))) for t in range(tick,tick+121)]
    context=dict(reference=reference,undo_tick=tick,advanced_tick=tick,resumed_tick=tick,continuation_end=tick+120,
        resumed_round="1" if tick==2504 else "0",suffix_text="",rows=lambda text,label:reference)
    for name,wanted in (("original",reference[:1]),("expected",reference[1:]),("observed",reference[1:])):
        selections=[n for n in ast.walk(tree) if isinstance(n,ast.Assign) and isinstance(n.value,ast.ListComp)
            and len(n.targets)==1 and isinstance(n.targets[0],ast.Name) and n.targets[0].id==name]
        assert len(selections)==1
        actual=eval(compile(ast.Expression(selections[0].value),"production selection","eval"),context)
        assert actual==wanted


@pytest.mark.parametrize("case",["native_clamp","matching_clamp","wrong_delta","unmatched_clamp","missing"])
def test_combat_widget_clock_native_completion_contract(case):
    import ast, inspect, replay_test
    tree=ast.parse(inspect.getsource(replay_test.compare_historical_combat))
    # Run the production rejection condition, not a replacement validator.
    condition=next(n for n in ast.walk(tree) if isinstance(n,ast.If)
        and any(isinstance(v,ast.Constant) and v.value=="damage widget still consumes held wall time or advances unexpectedly"
            for s in n.body if isinstance(s,ast.Raise) for v in ast.walk(s)))
    native=("5695","DmgTypeEff_C","0","0.016666668","0.016167600","0.350000018","0.350000024")
    row=list(native); row[4]="0.750000000" # Held wall time must not be consumed.
    if case=="wrong_delta": row[3]="0.750000000"
    if case=="unmatched_clamp": row[6]="0.350001024"
    context=dict(vars(replay_test),rows_clock=[] if case=="missing" else [tuple(row)],
        native_clock_rows=[native],owner="control" if case=="native_clamp" else "runtime")
    rejects=eval(compile(ast.Expression(condition.test),"production HUD check","eval"),context)
    assert bool(rejects)==(case in ("wrong_delta","unmatched_clamp","missing"))


@pytest.mark.native_contract
def test_production_visibility_accepts_only_verified_current_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.Visibility.inl').read_text(encoding='utf-8')
    start=source.index('                if(!found) {\n                    // Native1414FF1D0')
    end=source.index('            for(auto* query:visibility_a_.queries)',start)
    end=source.rfind('            }\n',start,end)
    (tmp_path/'visibility_owner_method.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_visibility_owner_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:visibility.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'visibility.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_hud_damage_backing_survives_native_shrink(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHudState.hpp").read_text(encoding="utf-8")
    start=source.index("    Status Install(const Sc6ReplayHudState& physical")
    end=source.index("        std::memcpy(&At<unsigned char>(damage_listener_",start)
    loop=source.index("        for(int i=0;i<pool_image_.count;++i)",end)
    stop=source.index("            reinterpret_cast<void(*)(void*,const void*)>",loop)
    methods=source[start:end]+source[loop:stop]+"        }\n        return Status::success();\n    }\n"
    marker="    // Damage backing transaction:"
    if marker in source:
        first=source.index(marker);last=source.index("    // Both images remain alive.",first)
        backing=source[first:last]
        begin=backing.find("    bool FinishDamagePlayer(")
        if begin>=0:
            finish=backing.index("    Status CommitDamageBackings()",begin)
            backing=backing[:begin]+backing[finish:]
        methods=backing+methods
    types=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHudState.ActiveTypes.inl').read_text(encoding='utf-8')
    methods+=types[types.index('bool RecoverTypeBackings('):types.index('bool CommitTypeBackings(')]
    (tmp_path / "hud_damage_methods.inl").write_text(methods,encoding="utf-8")
    fixture=module.ROOT / "tools/replay_hud_damage_backing_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:hud.exe /Fo:fixture.obj\n',encoding="utf-8")
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "hud.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


def test_late_damage_player_clock_comparison_uses_observed_continuation():
    from replay_test import compare_widget_continuation
    raw="\n".join(f"HUD clock tick={tick} widget=DmgValueEff_C player=0 delta=0.016666668 slate_delta=0.016 before={tick}.0 after={tick}.1 read_only=true" for tick in range(6539,6545))
    result=compare_widget_continuation(raw,raw,6538)
    assert result["active_player_updates"]==6 and result["first_tick"]==6539
    for broken in (raw.replace("6540.1","6540.2"),"\n".join(raw.splitlines()[1:]),raw+"\n"+raw.splitlines()[0]):
        with pytest.raises(RuntimeError):compare_widget_continuation(raw,broken,6538)


def test_widget_clock_preserves_multiple_native_widget_occurrences():
    from replay_test import compare_widget_continuation
    first="HUD clock tick=6574 widget=DmgValueEff_C player=0 delta=0.016666668 slate_delta=0.016 before=0.366666686 after=0.383333353 read_only=true"
    second=first.replace("before=0.366666686 after=0.383333353","before=0.000000000 after=0.000000000")
    raw=first+"\n"+second
    result=compare_widget_continuation(raw,raw,6538)
    assert result['active_player_updates']==2 and result['instance_identity']=='unobserved'
    for bad in (first,second+"\n"+first,raw+"\n"+first):
        with pytest.raises(RuntimeError):compare_widget_continuation(raw,bad,6538)


@pytest.mark.native_contract
def test_production_ground_commit_admission_and_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.GroundDebris.inl").read_text(encoding="utf-8")
    start=source.index("    bool owned_component(");end=source.index('#include "ReplayGroundCollisionDomain.inl"',start)
    native=source.index("    bool free_pointer(",start);continuation=source.index("    bool component_disposal(",native)
    (tmp_path / "ground_disposal_domain.inl").write_text(source[start:native]+source[continuation:end],encoding="utf-8")
    start=source.index("Status Sc6ReplayGroundDebrisState::PrepareCommit()");end=source.index("Status Sc6ReplayGroundDebrisState::VisitRoots",start)
    (tmp_path / "ground_commit_methods.inl").write_text(source[start:end],encoding="utf-8")
    host=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl").read_text(encoding="utf-8")
    start=host.index("        if (!transaction.retirement_requested)",host.index("retire(14,"))
    end=host.index("        if (transaction.prior_mode",start)
    (tmp_path / "ground_host_retirement_gate.inl").write_text(host[start:end],encoding="utf-8")
    fixture=module.ROOT / "tools/replay_ground_commit_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod/horselib"}" "{fixture}" /Fe:ground.exe /Fo:fixture.obj\n',encoding="utf-8")
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "ground.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr

def test_ground_commit_protocol_is_bounded():
    from deterministic_qualification.replay_fidelity import ground_commit_case
    from deterministic_qualification.replay_control import historical_capture_interval_supported
    report=dict(historical_anchor_tick=170,historical_advanced_tick=6538,host_seek_target=2510,host_seek=True,checkpoint_pair=False)
    assert ground_commit_case(report) and historical_capture_interval_supported(report)
    for change in (dict(historical_cancel='after'),dict(seek_drained_cancel=True),dict(seek_advance_failure=True),dict(seek_settlement_failure=True),dict(corrected_inputs=True),dict(source_revision=True),dict(host_seek_repeat=True),dict(historical_advanced_tick=5695),dict(host_seek_target=2511)):
        assert not ground_commit_case(dict(report,**change))

@pytest.mark.workflow
def test_ground_commit_cli_and_observation_budget(tmp_path,monkeypatch):
    import replay_test as module
    made=[]; original=module.control_args
    def args(*a,**kw):
        result=original(*a,**kw);made.append(result);return result
    monkeypatch.setattr(module,'control_args',args)
    monkeypatch.setattr(module.sys,'argv',['replay_test.py','combat-restore','--host-seek','--combat-case','commit','--combat-exact-advance','--combat-anchor-tick','170','--combat-advanced-tick','6538','--host-seek-target','2510','--coherence-images'])
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module,'ensure_build',lambda *_:{})
    seen=[]
    def capture(arg):
        seen.append(arg);raise RuntimeError('fixture stops before native launch')
    monkeypatch.setattr(module,'capture',capture)
    assert module.main()==1 and len(seen)==1
    assert len(made)==2 and all(x.observation_target==2510 for x in made)
    assert seen[0].watch_frames>=6538+(2510-170)+120

@pytest.mark.native_contract
def test_production_coherence_selection_covers_late_target(tmp_path):
    import re, subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / 'tools/replay_qualification_mod/ReplayPresentationObserver.hpp').read_text()
    held=re.search(r'if\((tick==208.*?)\) try \{',source).group(1)
    live=re.search(r'const bool coherence_tick=(.*?);',source,re.S).group(1)
    fixture=tmp_path/'selection.cpp'
    fixture.write_text('''#include <cassert>
struct Selection {unsigned extra_first_{2510};
 bool held(unsigned tick) {auto& self=*this;return '''+held+''';}
 bool live(unsigned tick,unsigned render_generation_) {bool coherence_=true;return '''+live+''';}
};
int main(){Selection s;assert(s.held(2510));assert(s.live(2532,0)&&s.live(2532,1));assert(s.live(2602,0)&&s.live(2602,1));assert(!s.live(2533,1));s.extra_first_=300;assert(s.held(300)&&s.live(400,1));s.extra_first_=208;assert(s.held(208)&&s.held(214)&&s.live(230,1)&&s.live(300,1));}
''')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{fixture}" /Fe:selection.exe /Fo:selection.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'selection.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr

def test_ground_commit_pose_window_reports_unobserved_anchor_and_requires_full_suffix():
    from replay_test import compare_native_poses
    def stream(run,generation,ticks):
        return '\n'.join(f'native pose publication run_id={run} tick={t} generation={generation} ordinal=0 player=0 bones=4 mapping=abc hash=def\nnative pose completed run_id={run} tick={t} generation={generation} evaluations=1' for t in ticks)
    ticks=list(range(171,450))+list(range(2504,2631))
    native=stream('n',0,ticks)
    restored=stream('r',0,range(185,205))+'\n'+stream('r',1,ticks)
    reports=[dict(run_id='n',observation_target=2510),dict(run_id='r',observation_target=2510,host_seek=True,host_seek_target=2510,historical_anchor_tick=170,historical_advanced_tick=6538)]
    result=compare_native_poses([native,restored],reports,2510,'')
    assert result['result']=='pass' and result['continuation_ticks']==120
    assert result['anchor_resimulation_coverage']['unobserved_ranges']==[[450,2503]]
    for tick in (449,2504,2511,2630):
        missing='\n'.join(x for x in restored.splitlines() if f'tick={tick} ' not in x)
        with pytest.raises(RuntimeError,match='missing native pose'):compare_native_poses([native,missing],reports,2510,'')
    changed=restored.replace('tick=2600 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=def','tick=2600 generation=1 ordinal=0 player=0 bones=4 mapping=abc hash=bad')
    assert compare_native_poses([native,changed],reports,2510,'')['result']=='fail'

def test_coherence_readback_protocol_tracks_selected_target():
    from replay_test import coherence_readback_expected
    late=dict(coherence_gpu_readbacks=True,observation_target=2510)
    for generation in (0,1):
        assert coherence_readback_expected(late,2532,generation)
        assert coherence_readback_expected(late,2602,generation)
        assert not coherence_readback_expected(late,230,generation)
    assert coherence_readback_expected(late,190,0) and not coherence_readback_expected(late,190,1)
    assert not coherence_readback_expected(dict(late,coherence_gpu_readbacks=False),2532,1)
    assert coherence_readback_expected(dict(coherence_gpu_readbacks=True,source_revision_profile='guard201'),400,1)
    assert coherence_readback_expected(dict(coherence_gpu_readbacks=True),230,1)

def test_late_coherence_requires_actual_resumed_images():
    from replay_test import coherence_capture_coverage
    native=dict(run_id='n',coherence_images={},observation_target=2510)
    candidate=dict(run_id='c',coherence_images_requested=True,host_seek_target=2510,observation_target=2510,coherence_images={})
    def add(report,tick,held,generation):
        report['coherence_images'][str((tick,held,generation))]=dict(run_id=report['run_id'],tick=tick,held=held,generation=generation,gpu_complete=True)
    add(candidate,2510,True,1)
    for tick in (2532,2602):add(native,tick,False,0);add(candidate,tick,False,1)
    assert coherence_capture_coverage(native,candidate)['result']=='pass'
    candidate['coherence_images'][str((2602,False,1))]['generation']=0
    assert coherence_capture_coverage(native,candidate)['missing']==['executor:native:2602']


@pytest.mark.native_contract
def test_production_optional_checkpoint_observer(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('        if(work.failure!=Horse::Deterministic::FailureCode::None',source.index('    void ObserveHostIndexCheckpoint('))
    end=source.index('        if(index_capture_==IndexCapture::Capturing && work.phase==Phase::RetiringScratch)',start)
    (tmp_path/'optional_checkpoint_observer.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_optional_checkpoint_observer_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:optional.exe /Fo:optional.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'optional.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_natural_checkpoint_fallback_observer(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=source.index('            if(seek.checkpoint<nearest) {')
    end=source.index('            const auto rewind_ticks=',start)
    (tmp_path/'natural_checkpoint_fallback.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_natural_fallback_observer_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:fallback.exe /Fo:fallback.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'fallback.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_expired_owner_role_uses_native_request_kind(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text()
    start=source.index('        // Distinguish a persistent manager request')
    end=source.index('        // Only immutable captured metadata',start)
    (tmp_path/'owner_role.inl').write_text(source[start:end])
    fixture=tmp_path/'owner_role.cpp'
    fixture.write_text(r'''#include "Sc6ReplayObjectLease.hpp"
#include <cassert>
#include <cstring>
using Horse::Deterministic::Sc6ReplayObjectLease;
template<class T> T At(const std::byte* p,std::size_t offset){T value;std::memcpy(&value,p+offset,sizeof(value));return value;}
struct Slot {std::array<std::byte,0xc0> bytes{};};
struct Emitter {std::uintptr_t component;};
struct State {std::size_t constructed_{};std::vector<Slot> slots_;std::vector<Emitter> cpu_emitters_,gpu_owners_;};
Sc6ReplayObjectLease::FailureWitness read(const State& state,std::uintptr_t address){
 struct Context {const State* state;};Context context{&state};Sc6ReplayObjectLease::FailureWitness witness;
 #include "owner_role.inl"
 return witness;
}
template<class T> void put(Slot& slot,std::size_t offset,T value){std::memcpy(slot.bytes.data()+offset,&value,sizeof(value));}
int main(){State state;state.slots_.resize(3);state.constructed_=3;
 // Native 1408A3C90: component+0, slot ID+8, request mesh ID+10, kind+14.
 put(state.slots_[0],0,std::uintptr_t{0x1234});put(state.slots_[0],8,std::int32_t{77});
 put(state.slots_[0],0x10,std::int32_t{0x12345678});put(state.slots_[0],0x14,std::uint8_t{5});
 state.slots_[1]=state.slots_[0];state.cpu_emitters_={{0x1234},{0x9999},{0x1234}};state.gpu_owners_={{0x1234}};
 auto result=read(state,0x1234);assert(result.captured_manager_slots==2);assert(result.captured_slot_id==77);
 assert(result.captured_slot_kind==5);assert(result.captured_cpu_emitters==2 && result.captured_gpu_emitters==1);
 result=read(state,0);assert(result.captured_manager_slots==0 && result.captured_slot_id==-1);
 result=read(state,0x9999);assert(result.captured_manager_slots==0 && result.captured_cpu_emitters==1);
}
''')
    batch=tmp_path/'compile.cmd'
    includes=module.ROOT/'HorseMod/horselib/deterministic'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{includes}" /I"{module.BUILD / "HorseMod/generated"}" "{fixture}" /Fe:owner_role.exe /Fo:owner_role.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner_role.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.workflow
@pytest.mark.parametrize('anchor,original,target,extra,allowed',[
    (6415,6417,6415,[],True), (3989,3991,3990,[],True),
    (6417,6419,6417,["--resume-candidate"],True),
    (6417,6419,6418,["--combat-case","before"],True),
    (6417,6419,6418,["--combat-case","after"],True),
    (6415,6415,6415,[],False), (6415,6536,6415,[],False),
    (6415,6417,6414,[],False), (6415,6417,6418,[],False),
    (6415,6417,6415,['--corrected-inputs'],False),
    (6415,6417,6415,['--host-seek-repeat'],False),
    (6415,6417,6415,['--seek-settlement-failure'],False),
])
def test_short_checkpoint_window_cli(tmp_path,monkeypatch,anchor,original,target,extra,allowed):
    import replay_test as module
    made=[]; create=module.control_args
    def args(*a,**kw):
        result=create(*a,**kw);made.append(result);return result
    monkeypatch.setattr(module,'control_args',args)
    monkeypatch.setattr(module.sys,'argv',['replay_test.py','combat-restore','--host-seek',
        '--combat-case','commit','--combat-exact-advance','--combat-anchor-tick',str(anchor),
        '--combat-advanced-tick',str(original),'--host-seek-target',str(target)]+extra)
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    built=[];monkeypatch.setattr(module,'ensure_build',lambda *_:built.append(True) or {})
    seen=[]
    def capture(arg):
        seen.append(arg);raise RuntimeError('fixture stops before native launch')
    monkeypatch.setattr(module,'capture',capture)
    monkeypatch.setattr(module,'revalidate_advance_failure_capture',capture)
    if allowed:
        assert module.main()==1 and len(seen)==1
        expected_observation=original if "--combat-case" in extra else target
        assert len(made)==2 and all(x.observation_target==expected_observation for x in made)
        assert min(x.watch_frames for x in made)>=original+120
        assert seen[0].historical_anchor_tick==anchor and seen[0].historical_advanced_tick==original
    else:
        with pytest.raises(SystemExit):module.main()
        assert not built and not seen


@pytest.mark.native_contract
def test_production_short_checkpoint_window_decoder(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text(encoding='utf-8')
    first=source.index('    bool probe_historical_restore{};')
    last=source.index('    bool pass_diagnostics{};',first)+len('    bool pass_diagnostics{};')
    begin=source.index('        if(fields.contains("host_seek_target")) {')
    end=source.index('        if (fields.contains("historical_cancel"))',begin)
    exact_begin=source.index('        if (fields.contains("historical_exact_advance"))')
    exact_end=source.index('        if (fields.contains("historical_single_step"))',exact_begin)
    (tmp_path/'coordinates.inl').write_text(source[exact_begin:exact_end]+source[begin:end])
    code=r'''#include <algorithm>
#include <cstdint>
#include <cassert>
#include "ReplayFailureProtocol.hpp"
#include "ReplayRollingCorrectionRequest.hpp"
struct ReplayPresentationObserver {static constexpr unsigned observation_last_tick=400;};
struct Request {'''+source[first:last]+r'''};
bool parse(std::map<std::string,std::string>& fields,Request& output){
#include "coordinates.inl"
return true;}
int main(){
 std::map<std::string,std::string> valid{{"host_seek","true"},{"historical_exact_advance","true"},
 {"historical_anchor_tick","6415"},{"historical_advanced_tick","6417"},{"host_seek_target","6415"}};
 auto read=[&](auto fields){Request out;out.host_seek=out.probe_historical_restore=true;
 const auto before=fields;const bool ok=parse(fields,out);if(ok){assert(fields==before);assert(out.historical_exact_advance&&out.historical_anchor_tick==std::stoul(fields.at("historical_anchor_tick"))&&out.historical_advanced_tick==std::stoul(fields.at("historical_advanced_tick"))&&out.host_seek_target==std::stoul(fields.at("host_seek_target")));}return ok;};
 assert(read(valid));
 auto early=valid;early["historical_anchor_tick"]="210";early["historical_advanced_tick"]="217";early["host_seek_target"]="217";assert(read(early));
 for(const auto* point:{"before","after"}) {auto cancelled=valid;cancelled["historical_cancel"]=point;assert(read(cancelled));}
 for(const auto* key:{"historical_anchor_tick","historical_advanced_tick","host_seek_target"})
 for(const auto* value:{"","-1","35001","4294967297","06415","6415x"}) {auto bad=valid;bad[key]=value;assert(!read(bad));}
 for(const auto* key:{"historical_cancel","corrected_inputs","host_seek_repeat","seek_settlement_failure","checkpoint_pair"}) {auto bad=valid;bad[key]="true";assert(!read(bad));}
}
'''
    (tmp_path/'decoder.cpp').write_text(code)
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.BUILD / "HorseMod/generated"}" /I"{module.ROOT / "tools/replay_qualification_mod"}" decoder.cpp /Fe:decoder.exe /Fo:decoder.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'decoder.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.parametrize('point',["before","after"])
def test_short_checkpoint_cancellation_has_no_execution_rewind(point):
    from tools.deterministic_qualification.replay_fidelity import bounded_checkpoint_window
    from tools.deterministic_qualification.replay_control import expected_historical_rewinds
    report=dict(historical_anchor_tick=6417,historical_advanced_tick=6419,host_seek_target=6418,
        host_seek=True,historical_exact_advance=True,historical_cancel=point)
    assert bounded_checkpoint_window(report)
    assert expected_historical_rewinds(report)==[]
    report['historical_cancel']=''
    assert expected_historical_rewinds(report)==[(6419,6417)]
    for invalid in ('true','during',True,None):
        assert not bounded_checkpoint_window(dict(report,historical_cancel=invalid))


@pytest.mark.parametrize('point',["before","after"])
def test_production_short_checkpoint_cancel_request_writer(point):
    import ast
    from tools.deterministic_qualification import replay_control
    tree=ast.parse(Path(replay_control.__file__).read_text(encoding="utf-8"))
    writer=next(node for node in ast.walk(tree) if isinstance(node,ast.If)
        and ast.unparse(node.test)=="report['probe_historical_restore']"
        and any(isinstance(child,ast.AugAssign) and ast.unparse(child.target)=="executor_options"
                for child in node.body))
    report=dict(probe_historical_restore=True,historical_anchor_tick=6417,historical_advanced_tick=6419,
        host_seek_target=6418,host_seek=True,historical_exact_advance=True,historical_single_step=False,
        historical_cancel=point,host_seek_repeat=False)
    scope=dict(replay_control.__dict__,report=report,executor_options="")
    exec(compile(ast.Module(body=[writer],type_ignores=[]),"production_request_writer","exec"),scope)
    assert scope["executor_options"].splitlines()==["probe_historical_restore=true","historical_anchor_tick=6417",
        "historical_advanced_tick=6419","historical_exact_advance=true","historical_cancel="+point]


@pytest.mark.native_contract
def test_production_checkpoint_window_pause_trigger(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text(encoding='utf-8')
    start=source.index('        if (!combat_pause_armed_ && !request_.probe_small_restore')
    end=source.index('\n        {',start)
    predicate=source[start:end].strip()[3:]
    fixture=tmp_path/'trigger.cpp'
    fixture.write_text('#include <cassert>\nstruct Request {bool probe_consumer_task{},probe_small_restore{},probe_interior_pause{},probe_application_pause{true},probe_historical_restore{true},host_seek{true},historical_exact_advance{true}; unsigned historical_anchor_tick{6415};} request_;\nstruct Sample {bool source_active{true};unsigned round{2},round_state{2},world_mode{2},frame{6414},round_frame{42};} sample;\nbool combat_pause_armed_=false,executor_started_=true;\nbool trigger(){return '+predicate+';}\nint main(){assert(trigger());sample.frame=6413;assert(!trigger());sample.frame=6414;sample.round_state=1;assert(!trigger());sample.round_state=2;request_.probe_historical_restore=false;assert(!trigger());sample.round=0;assert(trigger());request_.probe_consumer_task=true;assert(!trigger());sample.frame=209;assert(trigger());sample.frame=210;assert(!trigger());}\n',encoding='utf-8')
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{fixture}" /Fe:trigger.exe /Fo:trigger.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'trigger.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_checkpoint_window_queues_advance_before_resume(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text(encoding='utf-8')
    begin=source.index('if(UsesOwnedHistoricalAdvance())',source.index('interior_release_requested_ = true;',source.index('historical A captured run_id=')))
    end=source.index('{',begin)+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    block=source[begin:end]
    methods=source[source.index('    bool HasSecondCheckpoint() const'):source.index('    bool ObserveSecondCheckpoint(',source.index('    bool HasSecondCheckpoint() const'))]
    begin_hold=source.index('    bool ObserveHistoricalCombatRestore(const ReplayHost::InteriorWitness& held)')
    prelude=source[begin_hold:source.index('        const bool external_gpu_diagnostic=',begin_hold)].replace('ObserveHistoricalCombatRestore(', 'OriginalCallbackAdmitted(')+'return true; }\n'
    methods+=prelude
    code=r'''
#include <cassert>
#include <cstdint>
#include <string>
namespace ReplayHost {
enum class PauseBoundary {SimulationTick,CompletedApplication};
enum class TickAdvancePhase {Releasing,Settling,Held,Failed};
struct InteriorWitness {bool surface_pending{},application_idle{true},engine_idle{true},world_idle{true},pending_task{};std::uint64_t tick{};PauseBoundary boundary{PauseBoundary::CompletedApplication};};
using InteriorObserver=bool(*)(void*,const InteriorWitness&);
struct TickAdvanceWitness {TickAdvancePhase phase{TickAdvancePhase::Failed};};
}
#define STR(x) x
namespace RC {template<class T> const T& to_generic_string(const T& v){return v;}}
enum class LogLevel {Default};struct Output {template<LogLevel, class...T> static void send(T...) {}};
using Advance=bool(*)(std::uint64_t,void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*);
using Complete=bool(*)(void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness*);
static unsigned queued{},completed{};static bool accept=true;static std::uint64_t target{};
static bool advance(std::uint64_t t,void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness* w){++queued;target=t;w->phase=accept?ReplayHost::TickAdvancePhase::Releasing:ReplayHost::TickAdvancePhase::Failed;return accept;}
static bool complete(void*,ReplayHost::InteriorObserver,ReplayHost::TickAdvanceWitness* w){++completed;w->phase=accept?ReplayHost::TickAdvancePhase::Settling:ReplayHost::TickAdvancePhase::Failed;return accept;}
template<class T> T ResolveHorseModExport(const char* name){return reinterpret_cast<T>(std::string(name)=="horsemod_request_replay_tick"?reinterpret_cast<void*>(&advance):reinterpret_cast<void*>(&complete));}
struct ReplayQualificationMod {
 struct Request {bool checkpoint_pair{},checkpoint_fallback{},host_seek{true},historical_exact_advance{true};unsigned historical_anchor_tick{6415},historical_advanced_tick{6417};std::string run_id{"test"};}request_;
 bool application_reentry_armed_{},pair_B_armed_{},particle_copy_started_{true},interior_started_{true},interior_release_requested_{true};
 bool failed{};unsigned capturedB{};
 void Fail(const char*){failed=true;}
 bool ObserveSecondCheckpoint(const ReplayHost::InteriorWitness&){assert(false);return false;}
 bool ObserveHistoricalCombatRestore(const ReplayHost::InteriorWitness&){++capturedB;return false;}
'''+methods+'\n bool dispatch(){\n'+block+'\n return true; }\n};\n'+r'''
int main(){
 ReplayQualificationMod good;assert(!good.dispatch());assert(queued==1&&target==6417&&good.application_reentry_armed_&&!good.failed);
 ReplayHost::InteriorWitness outgoing;outgoing.tick=6415;assert(!good.OriginalCallbackAdmitted(outgoing));
 ReplayHost::InteriorWitness b;b.tick=6417;b.surface_pending=true;
 good.ObservePairOriginalBoundary(b);assert(!good.capturedB&&!completed);
 b.surface_pending=false;b.boundary=ReplayHost::PauseBoundary::SimulationTick;
 good.ObservePairOriginalBoundary(b);assert(completed==1&&!good.capturedB&&!good.pair_B_armed_);
 b.boundary=ReplayHost::PauseBoundary::CompletedApplication;
 good.ObservePairOriginalBoundary(b);assert(good.capturedB==1&&good.pair_B_armed_&&!good.interior_release_requested_);
 ReplayQualificationMod rejected;accept=false;assert(!rejected.dispatch());assert(rejected.failed&&!rejected.application_reentry_armed_);
 ReplayQualificationMod incomplete;b.pending_task=true;incomplete.ObservePairOriginalBoundary(b);assert(incomplete.failed&&!incomplete.capturedB);
 ReplayQualificationMod wrong;b.pending_task=false;b.tick=206;wrong.ObservePairOriginalBoundary(b);assert(wrong.failed&&!wrong.capturedB);
}
'''
    fixture=tmp_path/'advance.cpp';fixture.write_text(code,encoding='utf-8')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{fixture}" /Fe:advance.exe /Fo:advance.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'advance.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.parametrize("anchor",[205,6417])
def test_actual_checkpoint_hold_parser(anchor):
 import re,textwrap
 source=Path("tools/replay_test.py").read_text(encoding="utf-8")
 begin=source.index("            completed_holds=re.findall(")
 end=source.index("        lifecycle=",begin)
 block=textwrap.dedent(source[begin:end])
 def check(tick,updates=30,pending="false"):
  text=f"checkpoint pause held run_id=c native_frame={tick} epoch=1 elapsed_us=1000000 application_updates={updates} surface_frames=30 surface_bytes=4096 unchanged=true pending_event={pending} application_idle=true"
  exec(block,{"re":re,"texts":["",text],"executor":{"run_id":"c"},"anchor":anchor})
 check(anchor)
 for tick,updates,pending in [(anchor+1,30,"false"),(anchor,29,"false"),(anchor,30,"true")]:
  with pytest.raises(RuntimeError):check(tick,updates,pending)


@pytest.mark.native_contract
def test_production_render_preparation_precedes_emitter_owners(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp").read_text(encoding="utf-8")
    method=source[source.index("bool Sc6ReplayParticleCopy::PrepareTargetAtB"):source.index("bool Sc6ReplayParticleCopy::InstallCaptured")]
    (tmp_path / "method.inl").write_text(method)
    # The native host queues PrepareRenderTarget before PrepareHistoricalRestore
    # constructs emitter-owner rows. Test that actual render entry with rows
    # absent, without granting LocalFieldOwnersBound a fabricated success.
    (tmp_path / "fixture.cpp").write_text(r"""
#include <chrono>
#include <cstdint>
#include <cstdio>
#define STR(x) x
#define E_INVALIDARG 1
#define DXGI_ERROR_WAIT_TIMEOUT 2
namespace RC {enum class LogLevel{Warning};struct Output{template<LogLevel,class... T>static void send(const char*,T...) {}};}
struct ReplayGpuCompletion {using Clock=std::chrono::steady_clock;};
struct Sc6ReplayParticleCopy {
 enum class Phase{UndoReady};
 struct {Phase phase=Phase::UndoReady;bool undo_ready=true,target_prepared=false,native_write_uncommitted=false,deadline_expired=false;}witness_;
 struct {bool done=true;bool retired()const{return done;}}completion_;
 ReplayGpuCompletion::Clock::time_point request_deadline_=ReplayGpuCompletion::Clock::now()+std::chrono::seconds(10);
 bool bindings=true,owners=false;unsigned owner_checks=0,preparations=0;
 bool Bindings(void*,bool){return bindings;}
 bool LocalFieldOwnersBound(){++owner_checks;return owners;}
 bool PrepareLighting(std::size_t){++preparations;return true;}
 bool PruneOrphanVisibility(std::size_t){return true;}
 bool PrepareVisibilityStorage(std::size_t){return true;}
 bool PublishVisibility(bool,bool){return true;}
 bool PublishMaterial(bool,bool){return true;}
 bool PublishLighting(bool,bool){return true;}
 void Fail(int){}
 bool PrepareTargetAtB(void*,std::size_t) noexcept;
};
#include "method.inl"
int main(){
 Sc6ReplayParticleCopy p;
 p.completion_.done=false;
 if(p.PrepareTargetAtB(nullptr,100) || p.preparations)return 1;
 p.completion_.done=true;p.witness_.undo_ready=false;
 if(p.PrepareTargetAtB(nullptr,100) || p.preparations)return 2;
 p.witness_.undo_ready=true;
 if(!p.PrepareTargetAtB(nullptr,100) || !p.witness_.target_prepared || p.owner_checks){std::puts("render preparation incorrectly requires later emitter ownership");return 3;}
 if(p.PrepareTargetAtB(nullptr,100))return 4;
 return 0;
}
""")
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc fixture.cpp /Fe:test.exe\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "test.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


def test_trace_causality_uses_bounded_checkpoint_window(tmp_path):
    import json
    from replay_test import compare_same_process_trace,sha256_file
    raw=tmp_path/'trace.log';report=tmp_path/'report.json'
    lines=[f'trace source run_id=r generation={g} tick={t} player={p} states=1 expired=0 attached=1 fading=0 completion=0 pending=0 strong=1 child=0 membership=ab hash=cd'
           for g in (0,1) for t in (6418,6419) for p in (0,1)]
    def check(rows):
        raw.write_text('\n'.join(rows))
        report.write_text(json.dumps(dict(run_id='r',identities={},host_seek=True,historical_exact_advance=True,
            historical_anchor_tick=6417,historical_advanced_tick=6419,host_seek_target=6417,
            raw_log=dict(path=str(raw),sha256=sha256_file(raw)))))
        return compare_same_process_trace(report)
    result=check(lines)
    assert result['result']=='pass' and result['ticks']==2
    assert check(lines[:-1])['result']=='incomplete'
    bad=lines.copy();bad[4]=bad[4].replace('hash=cd','hash=ef')
    assert check(bad)['first_mismatch']['tick']==6418
    with pytest.raises(RuntimeError,match='duplicate'):check(lines+lines[:1])


@pytest.mark.parametrize("round_number", [0, 2, 3])
def test_paced_catchup_selection_uses_absolute_tick(round_number):
    import ast, inspect, replay_test
    tree=ast.parse(inspect.getsource(replay_test.compare_historical_combat))
    assignment=next(n for n in ast.walk(tree) if isinstance(n,ast.Assign)
        and any(isinstance(t,ast.Name) and t.id=="catchup_lines" for t in n.targets))
    line=f"[2026-09-14 10:00:01.000] [ReplayQualification] trajectory ordinal=1 frame=6419 round={round_number} hp=1"
    context=dict(vars(replay_test),advanced_tick=6419,suffix_text=line+"\n"+line.replace("frame=6419", "frame=6420"))
    assert eval(compile(ast.Expression(assignment.value),"production catchup selection","eval"),context)==[line]


def test_retained_cold_copy_cancel_expects_no_native_traversal():
    import ast
    import replay_test as module
    from deterministic_qualification.replay_control import expected_historical_rewinds
    function=next(n for n in ast.parse(Path(module.__file__).read_text(encoding='utf-8')).body
        if isinstance(n,ast.FunctionDef) and n.name=='revalidate_advance_failure_capture')
    statements=[n for n in function.body if isinstance(n,ast.Assign)
        and any(isinstance(t,ast.Name) and t.id in ('planned_rewinds','rows') for t in n.targets)]
    def check_boundary(*args,**kwargs):
        assert kwargs['historical_restore'] is False
        assert kwargs['expected_rewinds']==[]
        return []
    scope=dict(window=False,copy_matches=True,report={'historical_advanced_tick':210,'historical_anchor_tick':205,
        'host_seek_target':208,'historical_cancel':'before','host_seek_first_target':0,'checkpoint_pair':False,
        'host_seek_repeat':False},text='',run='r',expected_historical_rewinds=expected_historical_rewinds,
        validate_boundary_capture=check_boundary,expected_recovery_rewind=lambda *_:None)
    exec(compile(ast.Module(body=statements,type_ignores=[]),'retained_boundary_call','exec'),scope)


def test_verified_early_seven_tick_lifecycle_window():
    from deterministic_qualification.replay_fidelity import bounded_checkpoint_window
    from deterministic_qualification.replay_control import expected_historical_rewinds
    report=dict(historical_anchor_tick=210,historical_advanced_tick=217,host_seek_target=217,
        host_seek=True,historical_exact_advance=True,historical_cancel='')
    assert bounded_checkpoint_window(report)
    assert expected_historical_rewinds(report)==[(217,210)]
    for key,value in [('historical_anchor_tick',209),('historical_advanced_tick',218),
        ('host_seek_target',218),('historical_exact_advance',False),('changed_inputs',True)]:
        assert not bounded_checkpoint_window(dict(report,**{key:value}))


@pytest.mark.parametrize('code',[0xc0000005,259,None])
def test_capture_rejects_abnormal_exit_after_observation(code):
    import ast
    from deterministic_qualification import replay_control
    tree=ast.parse(Path(replay_control.__file__).read_text(encoding='utf-8'))
    body=next(n.body for n in ast.walk(tree) if isinstance(n,ast.With)
        and any(isinstance(c,ast.Expr) and isinstance(c.value,ast.Call)
            and isinstance(c.value.func,ast.Name) and c.value.func.id=='close_game' for c in n.body))
    begin=next(i for i,n in enumerate(body) if isinstance(n,ast.Expr) and isinstance(n.value,ast.Call)
        and isinstance(n.value.func,ast.Name) and n.value.func.id=='close_game')
    scope=dict(pid=1,report={},close_game=lambda *a,**k:None,exit_witness=SimpleNamespace(exit_code=lambda:code))
    block=compile(ast.Module(body=body[begin:],type_ignores=[]),'capture_native_exit','exec')
    with pytest.raises(RuntimeError,match='exit'):exec(block,scope)
    assert scope['report'].get('result')!='captured'
    scope['exit_witness']=SimpleNamespace(exit_code=lambda:0)
    exec(block,scope)
    assert scope['report']['result']=='captured'


def test_retained_bootstrap_rejects_completed_observation_with_crash(tmp_path,monkeypatch):
    import json
    import replay_test as module
    ids={'runtime':'r','framework':'f'}
    timing={'viewport_fps_milli':60000,'tick_rate_milli':60000}
    raw=tmp_path/'raw.log';raw.write_text('retained observation')
    report=dict(result='captured',mode='runtime',cleanup={'complete':True,'games_remaining':0},
        identities=ids,run_id='r',timing=timing,raw_log={'path':str(raw),'sha256':'hash'},
        loaded_runtime={'sha256':'r','verification':'owned_process_mapped_file_and_sha256'},
        loaded_framework={'sha256':'f','verification':'owned_process_mapped_file_and_sha256'},
        process_exit={'code':0xc0000005,'observed_before_owned_cleanup':True})
    monkeypatch.setattr(module,'OUTPUT',tmp_path)
    monkeypatch.setattr(module,'current_identities',lambda _:ids)
    monkeypatch.setattr(module,'sha256_file',lambda _: 'hash')
    monkeypatch.setattr(module,'validate_trajectory_timing',lambda *_:timing)
    path=tmp_path/'bootstrap.json'
    path.write_text(json.dumps(report))
    with pytest.raises(RuntimeError):module.require_bootstrap(SimpleNamespace())
    report['process_exit']['code']=0;path.write_text(json.dumps(report))
    module.require_bootstrap(SimpleNamespace())


@pytest.mark.native_contract
def test_online_rules_teardown_uses_owned_callback_ids_after_object_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/OnlineRules.hpp').read_text(encoding='utf-8')
    begin=source.index('        void uninstall_hooks()')
    end=source.index('    private:',begin)
    (tmp_path/'uninstall.inl').write_text(source[begin:end],encoding='utf-8')
    code=r'''#include <map>
#include <set>
#include <string>
#include <utility>
#include <cstdint>
#include <cassert>
#define STR(x) L##x
unsigned lookups{};
namespace RC {enum class LogLevel {Verbose};struct Output {template<LogLevel L,class...T>static void send(T...){}};
namespace Unreal {struct UFunction{};struct Data{std::set<int> ids;bool RemoveCallback(int id){return ids.erase(id)!=0;}};
namespace Internal {std::map<const UFunction*,Data> registry;auto& GetHookedFunctionsMap(){return registry;}}
namespace UObjectGlobals {void UnregisterHook(const std::wstring&,std::pair<int,int>){++lookups;}}}}
struct Subject {
 bool m_slipout_runtime_hook_registered=true;
 std::wstring m_slipout_runtime_hook_path=L"/Script/LuxorGame.LuxBattleMissionManager:IsSlipEnabled";
 std::pair<int32_t,int32_t> m_slipout_runtime_hook_ids{11,12};
 RC::Unreal::UFunction* m_slipout_runtime_hook_function=reinterpret_cast<RC::Unreal::UFunction*>(0x10);
#include "uninstall.inl"
};
int main(){
 auto* expired=reinterpret_cast<RC::Unreal::UFunction*>(0x10);
 auto& registry=RC::Unreal::Internal::GetHookedFunctionsMap();registry[expired].ids={11,12,88};
 Subject a;a.uninstall_hooks();assert(lookups==0);assert(registry[expired].ids==std::set<int>{88});
 assert(!a.m_slipout_runtime_hook_registered);a.uninstall_hooks();assert(registry[expired].ids==std::set<int>{88});
 registry.clear();Subject missing;missing.uninstall_hooks();assert(registry.empty()&&lookups==0);
}
'''
    (tmp_path/'uninstall.cpp').write_text(code)
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc uninstall.cpp /Fe:uninstall.exe /Fo:uninstall.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'uninstall.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_cold_owner_rejects_render_ownership_and_retries_only_lease_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayColdParticleOwner.hpp').read_text(encoding='utf-8')
    cold=source[source.index('    static bool Cold('):source.index('    static bool PassiveOuterModify(')]
    retire=source[source.index('    static bool Retire('):source.rindex('};')]
    (tmp_path/'cold.inl').write_text(cold+retire,encoding='utf-8')
    code=r'''#include <array>
#include <cstdint>
#include <cstring>
#include <cassert>
struct Status{bool value;bool ok(){return value;}};
struct Sc6ReplayObjectLease{bool release_ok=false;unsigned releases{};Status Validate(){return {true};} Status Release(){++releases;return {release_ok};}};
struct Subject {
 struct Witness {const char* check{};std::uintptr_t copy{},outer{};bool created=true,detached=true,destroyed=false,lease_released=false,material_construction_uncertain=false;};
 static inline bool live=true;static inline unsigned destroys{};
 template<class T>static T Read(std::uintptr_t p,std::size_t n=0){T v;std::memcpy(&v,reinterpret_cast<void*>(p+n),sizeof(v));return v;}
 static bool EmptyArray(std::uintptr_t p,std::size_t n){return Read<int>(p,n+8)==0;}
 static bool Live(void*){return live;}
 static bool NativeDestroy(std::uintptr_t,void*){++destroys;live=false;return true;}
#include "cold.inl"
};
int main(){
 std::array<std::byte,0xad0> object{};constexpr std::uintptr_t base=0x140000000,outer=0x123400;
 const auto address=reinterpret_cast<std::uintptr_t>(object.data());
 const auto put=[&](std::size_t n,auto v){std::memcpy(object.data()+n,&v,sizeof(v));};
 put(0,base+0x335db28);put(0x20,outer);put(0x190,outer);
 assert(Subject::Cold(base,address,outer));
 put(0x188,2u);assert(!Subject::Cold(base,address,outer));put(0x188,0u);
 put(0x790,std::uintptr_t{0x456700});assert(!Subject::Cold(base,address,outer));put(0x790,std::uintptr_t{});
 Sc6ReplayObjectLease lease;Subject::Witness w;w.copy=address;w.outer=outer;
 assert(!Subject::Retire(base,lease,w)&&w.destroyed&&!w.lease_released);
 assert(Subject::destroys==1&&lease.releases==1);
 lease.release_ok=true;assert(Subject::Retire(base,lease,w));
 assert(Subject::destroys==1&&lease.releases==2);
 assert(Subject::Retire(base,lease,w)&&Subject::destroys==1&&lease.releases==2);
}
'''
    (tmp_path/'cold.cpp').write_text(code,encoding='utf-8')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc cold.cpp /Fe:cold.exe /Fo:cold.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'cold.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_cold_clone_requires_exact_retained_source_lease(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayColdParticleOwner.hpp').read_text(encoding='utf-8')
    start=source.index('    static bool CloneDetached(')
    (tmp_path/'clone.inl').write_text(source[start:source.index('    static bool EquivalentDetached(',start)],encoding='utf-8')
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayObjectLease.cpp').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplayObjectLease::ValidateObject(')
    (tmp_path/'lease.inl').write_text(source[start:source.index('void Sc6ReplayObjectLease::DiagnoseFailure(',start)],encoding='utf-8')
    code=r'''#include <vector>
#include <algorithm>
#include <cstdint>
#include <cassert>
enum class FailureCode {IllegalTransition,ContextUnavailable,GenerationMismatch};
struct Status {bool value;bool ok()const{return value;}static Status success(){return {true};}static Status failure(FailureCode){return {false};}};
unsigned admission{},released{};bool available=true;
bool EnterAdmission(std::uintptr_t){if(!available)return false;++admission;return true;}void LeaveAdmission(std::uintptr_t){++released;}
struct Sc6ReplayObjectLease {
 struct Control {struct Reference{void* object;void* original;};std::vector<Reference> references;std::uintptr_t base{};
 bool live=true,member=true;bool Live()const{return live;}bool Membership()const{return member;}};
 Control* control_{};bool registered()const{return control_!=nullptr;}Status ValidateObject(const void*)const noexcept;
};
#include "lease.inl"
struct Subject {
 struct Witness {const char* check="unchanged";std::uintptr_t copy{},outer{};bool created=true,detached=true,destroyed=false,lease_released=false,material_construction_uncertain=false;};
 static inline bool cold=true;static inline unsigned constructions{};static inline void* copied{};
 static bool Cold(std::uintptr_t,std::uintptr_t,std::uintptr_t){return cold;}
 static bool ConstructDetachedImpl(std::uintptr_t,void* original,Sc6ReplayObjectLease&,Witness&,bool detached){assert(detached);++constructions;copied=original;return true;}
#include "clone.inl"
};
int main(){
 void* object=reinterpret_cast<void*>(0x10);Sc6ReplayObjectLease::Control control;control.references.push_back({object,object});
 Sc6ReplayObjectLease source_lease{&control},destination;Subject::Witness source,result;source.copy=0x10;source.outer=0x20;
 assert(Subject::CloneDetached(0,source,source_lease,destination,result));assert(Subject::constructions==1&&Subject::copied==object);
 const auto reject=[&]{const auto n=Subject::constructions;assert(!Subject::CloneDetached(0,source,source_lease,destination,result));assert(Subject::constructions==n&&admission==released);};
 control.references[0].original=reinterpret_cast<void*>(0x30);reject();control.references[0].original=object;
 control.references[0].object=nullptr;reject();control.references[0].object=object;
 control.live=false;reject();control.live=true;control.member=false;reject();control.member=true;
 available=false;reject();available=true;Subject::cold=false;reject();Subject::cold=true;
 source.detached=false;reject();source.detached=true;source.destroyed=true;reject();source.destroyed=false;
 source.lease_released=true;reject();source.lease_released=false;
 assert(!Subject::CloneDetached(0,source,source_lease,source_lease,result));
 assert(!Subject::CloneDetached(0,source,source_lease,destination,source));assert(source.check==std::string_view("unchanged"));
 assert(!source_lease.ValidateObject(nullptr).ok()&&admission==released);
 assert(Subject::CloneDetached(0,source,source_lease,destination,result));assert(Subject::constructions==2&&admission==released);
}
'''.replace('#include <cassert>','#include <cassert>\n#include <string_view>')
    (tmp_path/'clone.cpp').write_text(code,encoding='utf-8')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc clone.cpp /Fe:clone.exe /Fo:clone.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'clone.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_registration_journal_retains_partial_native_ownership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleComponentOwner.hpp').read_text(encoding='utf-8')
    declarations=source[source.index('    enum class Phase'):source.index('    enum class RenderPhase')]
    methods=source[source.index('    static bool RegisterInactive'):source.rindex('\n};')]
    (tmp_path/'registration_owner.inl').write_text(declarations+methods,encoding='utf-8')
    code=r'''#include <array>
#include <algorithm>
#include <cstdint>
#include <cassert>
struct Sc6ReplayObjectLease {bool release=true;unsigned releases{};struct Status{bool value;bool ok()const{return value;}};Status Release(){++releases;return {release};}};
struct Subject {
 struct Witness{std::uintptr_t copy=10,outer=20;bool detached=true,destroyed=false,lease_released=false;};
 using Members=std::array<std::uintptr_t,1024>;
 static inline Members members{30};static inline unsigned count=1,adds{},registrations{},destroys{};
 static inline bool admitted=true,add=true,registration=true,valid=true,destruction=true,live=true;
 template<class T>static T Read(std::uintptr_t,std::size_t off=0){if(off==0x298)return T(0x1f711a0);if(off==0x640)return T(0x1fbd100);if(off==0x808)return T(40);return T(0);}
 static bool RegistrationPreflight(std::uintptr_t,void*,Sc6ReplayObjectLease&,Witness&){return admitted;}
 static bool EndPlayEmpty(std::uintptr_t,std::uintptr_t){return true;}
 static bool ReadMembers(std::uintptr_t,Members& result,unsigned& n){result=members;n=count;return true;}
 static bool NativeAdd(std::uintptr_t,void*,void*){++adds;members[count++]=10;return add;}
 static bool NativeRegister(std::uintptr_t,void*,void*){++registrations;return registration;}
 template<class T>static bool InactiveRegistered(std::uintptr_t,const Sc6ReplayObjectLease&,const T&){return valid;}
 static bool NativeDestroy(std::uintptr_t,void*){++destroys;live=!destruction;return destruction;}
 static bool Live(void*){return live;}
#include "registration_owner.inl"
 static void Reset(){members={30};count=1;adds=registrations=destroys=0;admitted=add=registration=valid=destruction=live=true;}
};
int main(){using P=Subject::Phase;
 {Subject::Reset();Subject::Witness w;Subject::Registration s;Sc6ReplayObjectLease l;
  Subject::admitted=false;assert(!Subject::RegisterInactive(0,(void*)50,l,w,s)&&s.phase==P::Empty&&w.detached&&!Subject::adds);
  Subject::admitted=true;assert(Subject::RegisterInactive(0,(void*)50,l,w,s)&&s.phase==P::Registered&&!w.detached);
  assert(!Subject::RegisterInactive(0,(void*)50,l,w,s)&&Subject::adds==1&&Subject::registrations==1);
  l.release=false;assert(!Subject::RetireInactive(0,l,w,s)&&s.phase==P::Destroyed&&w.destroyed&&Subject::destroys==1);
  l.release=true;assert(Subject::RetireInactive(0,l,w,s)&&s.phase==P::Retired&&w.lease_released&&Subject::destroys==1);
  assert(Subject::RetireInactive(0,l,w,s)&&l.releases==2&&Subject::destroys==1);
 }
 {Subject::Reset();Subject::Witness w;Subject::Registration s;Sc6ReplayObjectLease l;Subject::add=false;
  assert(!Subject::RegisterInactive(0,(void*)50,l,w,s)&&s.phase==P::Adding&&!w.detached&&Subject::adds==1);
  assert(!Subject::RetireInactive(0,l,w,s)&&!l.releases&&!Subject::destroys);
  assert(!Subject::RegisterInactive(0,(void*)50,l,w,s)&&Subject::adds==1);
 }
 {Subject::Reset();Subject::Witness w;Subject::Registration s;Sc6ReplayObjectLease l;Subject::registration=false;
  assert(!Subject::RegisterInactive(0,(void*)50,l,w,s)&&s.phase==P::Registering);
  assert(!Subject::RetireInactive(0,l,w,s)&&!l.releases&&!Subject::destroys);
 }
 {Subject::Reset();Subject::Witness w;Subject::Registration s;Sc6ReplayObjectLease l;
  assert(Subject::RegisterInactive(0,(void*)50,l,w,s));Subject::valid=false;
  assert(!Subject::RetireInactive(0,l,w,s)&&s.phase==P::Registered&&!Subject::destroys);Subject::valid=true;
  Subject::destruction=false;assert(!Subject::RetireInactive(0,l,w,s)&&s.phase==P::Destroying&&Subject::destroys==1&&!l.releases);
  Subject::destruction=true;assert(!Subject::RetireInactive(0,l,w,s)&&Subject::destroys==1&&!l.releases);
 }
}
'''
    (tmp_path/'owner.cpp').write_text(code,encoding='utf-8')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc owner.cpp /Fe:owner.exe /Fo:owner.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_cold_registration_preflight_preserves_native_activation_and_callbacks(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayColdParticleOwner.hpp').read_text(encoding='utf-8')
    start=source.index('    static bool RegistrationCallbacksEmpty(')
    end=source.index('    static bool WorldMatches(',start)
    begin=source.index('    static bool RegistrationPreflight(',end)
    (tmp_path/'registration.inl').write_text(source[start:end]+source[begin:source.index('private:',begin)],encoding='utf-8')
    code=r'''#include <Windows.h>
#include <array>
#include <map>
#include <cstdint>
#include <cstring>
#include <cassert>
struct Status{bool value;bool ok()const{return value;}};
struct Sc6ReplayObjectLease {bool valid=true;Status ValidateObject(const void*)const{return {valid};}};
struct Subject {
 struct Witness{std::uintptr_t copy=0x1000,outer=0x2000;bool created=true,detached=true,destroyed=false,lease_released=false,material_construction_uncertain=false;};
 static inline std::uintptr_t base{};static inline std::map<std::uintptr_t,std::uint64_t> fields;
 static inline std::array<std::byte,0x100> function{};static inline bool cold=true,world=true,outer=true,found=true;
 static inline unsigned callbacks{},reads{};
 template<class T>static T Read(std::uintptr_t p,std::size_t offset=0){++reads;T v{};const auto a=p+offset;
 const auto first=reinterpret_cast<std::uintptr_t>(function.data());
 if(a>=first&&a+sizeof(v)<=first+function.size())std::memcpy(&v,reinterpret_cast<void*>(a),sizeof(v));
 else {const auto value=fields.at(a);std::memcpy(&v,&value,sizeof(v));}return v;}
 static bool Cold(std::uintptr_t,std::uintptr_t,std::uintptr_t){return cold;}
 static bool WorldMatches(std::uintptr_t,void*){return world;}
 static bool PassiveOuterModify(std::uintptr_t,std::uintptr_t){return outer;}
 static std::uintptr_t Find(const void* component,std::uint64_t name){assert(component==reinterpret_cast<void*>(0x1000)&&name==0x1234);++callbacks;return found?reinterpret_cast<std::uintptr_t>(function.data()):0;}
#include "registration.inl"
};
int main(){
 auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(code);
 const unsigned char jump[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xe0};std::memcpy(code,jump,sizeof(jump));
 const auto target=reinterpret_cast<std::uintptr_t>(&Subject::Find);std::memcpy(code+2,&target,8);DWORD old{};assert(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old));
 Subject::base=reinterpret_cast<std::uintptr_t>(code)-0xf6e0e0;const auto base=Subject::base,table=base+0x335db28;
 auto& f=Subject::fields;f[0x1000]=table;f[0x1808]=0x3000;f[0x1830]=0;f[0x13f8]=0;f[0x165e]=0;f[base+0x43b5530]=0x1234;
 for(auto [offset,rva]:std::array<std::pair<unsigned,unsigned>,7>{{{0x1f8,0xf71540},{0x280,0x1f7ae20},{0x2c8,0x1dae440},{0x2e0,0x1d4c640},{0x2e8,0x1d94480},{0x368,0x1d4fd40},{0x630,0x2d9bf0}}})f[table+offset]=base+rva;
 const auto before=f;Sc6ReplayObjectLease lease;Subject::Witness w;
 const auto check=[&]{return Subject::RegistrationPreflight(base,reinterpret_cast<void*>(0x4000),lease,w);};
 assert(check()&&Subject::callbacks==1&&f==before);
 for(auto [address,value]:std::array<std::pair<std::uintptr_t,std::uint64_t>,5>{{{0x1830,0x20},{0x1830,0x1000},{0x13f8,8},{0x165e,1},{0x1808,0}}}) {
  const auto saved=f[address];f[address]=value;const auto n=Subject::callbacks;assert(!check()&&Subject::callbacks==n);assert(f[address]==value);f[address]=saved;
 }
 const auto put=[](std::size_t n,unsigned value){std::memcpy(Subject::function.data()+n,&value,4);};
 put(0x88,0x400);assert(!check());put(0x88,0);put(0x50,1);assert(!check());put(0x50,0);
 Subject::found=false;assert(!check());Subject::found=true;
 f[table+0x368]+=8;const auto n=Subject::callbacks;assert(!check()&&Subject::callbacks==n);f[table+0x368]-=8;
 f[table+0x630]+=8;const auto body_callbacks=Subject::callbacks;assert(!check()&&Subject::callbacks==body_callbacks);f[table+0x630]-=8;
 lease.valid=false;const auto r=Subject::reads;assert(!check()&&Subject::reads==r);lease.valid=true;
 Subject::cold=false;assert(!check());Subject::cold=true;Subject::world=false;assert(!check());Subject::world=true;
 Subject::outer=false;assert(!check());Subject::outer=true;w.detached=false;assert(!check());w.detached=true;
 assert(check()&&f==before);VirtualFree(code,0,MEM_RELEASE);
}
'''
    (tmp_path/'registration.cpp').write_text(code,encoding='utf-8')
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc registration.cpp /Fe:registration.exe /Fo:registration.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'registration.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_configuration_deferred_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleConfiguration.hpp').read_text(encoding='utf-8')
    source=source.replace('#include "Sc6ReplayColdParticleOwner.hpp"','')
    (tmp_path/'particle_configuration.inl').write_text(source)
    host=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Checkpoint.inl').read_text(encoding='utf-8')
    start=host.index('            const auto status=Sc6ReplayParticleConfiguration::Capture(')
    (tmp_path/'particle_configuration_capture_route.inl').write_text(host[start:host.index('            if(const auto& owner=',start)],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_particle_configuration_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:configuration.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'configuration.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_scheduler_reconstruction_private_projection(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.cpp').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplaySchedulerState::RehashTickSetStorage(')
    end=source.index('bool Sc6ReplaySchedulerState::OwnsStageComponent(',start)
    (tmp_path/'scheduler_reconstruction.inl').write_text(source[start:end])
    fixture=module.ROOT/'tools/replay_scheduler_reconstruction_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:scheduler.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'scheduler.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_configuration_managed_selection(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplayVfxState::VisitSingleGpuComponents(')
    end=source.index('Status Sc6ReplayVfxState::ValidateReconstructionBindings(',start)
    (tmp_path/'particle_configuration_selection.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_particle_configuration_selection_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:selection.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'selection.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_observer_releases_readback_pin_before_seek_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text(encoding='utf-8')
    start=source.index('    void ReleaseHostSeekAndResume(')
    end=source.index('    void ObserveParticleCopyResume(',start)
    helper=source.index('    std::uint64_t HostSeekExpectedCheckpoint(')
    helper_end=source.index('    void ObserveHostSeek(',helper)
    (tmp_path/'observer_release.inl').write_text(source[helper:helper_end]+source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_observer_release_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:release.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'release.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_manager_reconstruction_preserves_original_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    pieces=[]
    for start,end in [('Status Sc6ReplayVfxState::PreparedManager::ValidateComponents(', 'Status Sc6ReplayVfxState::PreparedManager::ValidateImage('),('bool Sc6ReplayVfxState::PreparedManager::Write(', 'Status Sc6ReplayVfxState::PreparedManager::Publish(')]:
        a=source.index(start);pieces.append(source[a:source.index(end,a)])
    (tmp_path/'manager_reconstruction.inl').write_text('\n'.join(pieces),encoding='utf-8')
    fixture=module.ROOT/'tools/replay_manager_reconstruction_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:manager.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'manager.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_manager_slot_reconstruction_typed_fields(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::ProjectManagerSlot(')
    b=source.index('Status Sc6ReplayVfxState::ProjectManagerComponent(',a)
    (tmp_path/'manager_slot.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_manager_slot_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:slot.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'slot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_fresh_gpu_native_death_receipts_never_read_freed_owners(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayCpuEmitterState.cpp').read_text(encoding='utf-8')
    a=source.index('unsigned Sc6ReplayCpuEmitterState::Prepared::DestructionEntryFailure(')
    b=source.index('void* volatile* Sc6ReplayCpuEmitterState::Prepared::ResolveDestroyedSlot(',a)
    pieces=[source[a:b]]
    for start,end in [('Status Sc6ReplayCpuEmitterState::Prepared::SettleFreshGpuDestruction(', 'Status Sc6ReplayCpuEmitterState::Prepared::SettleNativeDestruction('),('std::uint64_t Sc6ReplayCpuEmitterState::Prepared::BackingFingerprint(', 'void* volatile* Sc6ReplayCpuEmitterState::Prepared::ResolveSlot('),('Status Sc6ReplayCpuEmitterState::Prepared::ValidatePublished(', 'Status Sc6ReplayCpuEmitterState::Prepared::UndoPublication(')]:
        begin=source.index(start);pieces.append(source[begin:source.index(end,begin)])
    (tmp_path/'gpu_death_receipt.inl').write_text('\n'.join(pieces),encoding='utf-8')
    for start,end in [('Status Sc6ReplayCpuEmitterState::Prepared::SettleNativeDestruction(', 'Sc6ReplayCpuEmitterState::Prepared::~Prepared('),('Status Sc6ReplayCpuEmitterState::Prepared::UndoPublication(', 'Status Sc6ReplayCpuEmitterState::Prepared::CommitPublication('),('Status Sc6ReplayCpuEmitterState::Prepared::ReopenExecutionForUndo(', 'bool Sc6ReplayCpuEmitterState::StorageDisjoint(')]:
        begin=source.index(start);pieces.append(source[begin:source.index(end,begin)])
    (tmp_path/'gpu_death_receipt.inl').write_text('\n'.join(pieces),encoding='utf-8')
    fixture=module.ROOT/'tools/replay_gpu_death_receipt_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:death.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'death.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_coordinate_command_is_outside_its_flushed_list(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Surface.inl').read_text(encoding='utf-8')
    a=source.index('void Sc6ReplayHost::QueueParticleCopyCommand()')
    b=source.index('bool Sc6ReplayHost::ReopenCapturedImageAtB()',a)
    (tmp_path/'particle_command_order.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_particle_command_order_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:command.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'command.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_scheduler_settled_validation_allows_natural_a_death(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.Storage.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplaySchedulerState::ValidatePublishedBacking(')
    b=source.index('Status Sc6ReplaySchedulerState::PreparedRestore::ValidatePrivateOwners(',a)
    (tmp_path/'scheduler_settled.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_scheduler_settled_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:settled.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'settled.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_scheduler_private_constructor_backing_retires_after_execution_undo(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.Storage.cpp').read_text(encoding='utf-8')
    start=source.index('void Sc6ReplaySchedulerState::PreparedRestore::Clear(')
    end=source.index('std::size_t Sc6ReplaySchedulerState::PreparedRestore::owned_bytes(',start)
    (tmp_path/'scheduler_private_prerequisite.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_scheduler_private_prerequisite_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:retirement.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'retirement.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_scheduler_added_tick_never_enters_complete_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.Storage.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplaySchedulerState::PreparedRestore::ValidateAddedTicks(')
    b=source.index('Status Sc6ReplaySchedulerState::ValidateCurrentImage(',a)
    (tmp_path/'scheduler_added_tick.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_scheduler_added_tick_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:added.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'added.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_reconstructed_topology_preserves_complete_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::PrepareTopology(')
    b=source.index('Sc6ReplayVfxState::PreparedManager::~PreparedManager()',a)
    lifetime=source[source.index('Status Sc6ReplayVfxState::ParticleBirth::ValidateLifetime()'):source.index('Status Sc6ReplayVfxState::ParticleBirth::ValidateCurrentB()')]
    (tmp_path/'reconstructed_topology.inl').write_text(lifetime+source[a:b],encoding='utf-8')
    recovery=source.index('Status Sc6ReplayVfxState::PrepareRecoveryRetirement(')
    recovery_end=source.index('Status Sc6ReplayVfxState::PrepareCoordinateReconstruction(',recovery)
    (tmp_path/'recovery_topology.inl').write_text(source[recovery:recovery_end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_reconstructed_topology_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:slot.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'slot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr



@pytest.mark.native_contract
def test_mapped_emitter_set_keeps_surviving_cpu_owners(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::PrepareCpuEmitters(')
    b=source.index('std::uint64_t Sc6ReplayVfxState::storage_fingerprint()',a)
    (tmp_path/'mapped_emitter_set.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_mapped_emitter_set_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:slot.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'slot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr



@pytest.mark.native_contract
def test_mapped_tile_pool_preserves_original_free_order(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::ValidatePoolTransaction(')
    b=source.index('Status Sc6ReplayVfxState::CaptureDefinitionMap(',a)
    owned=source[source.index('std::size_t Sc6ReplayVfxState::PreparedTilePools::owned_bytes()'):source.index('bool Sc6ReplayVfxState::PreparedTilePools::published()')]
    header=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.hpp').read_text(encoding='utf-8')
    (tmp_path/'pool_partition.inl').write_text(header[header.index('    static bool IsCompleteTilePartition('):header.index('    static Status ValidateNativeTilePartition(')],encoding='utf-8')
    (tmp_path/'mapped_tile_pool.inl').write_text(owned+source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_mapped_tile_pool_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:slot.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'slot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr



@pytest.mark.native_contract
def test_reconstruction_inventory_is_separate_from_complete_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::PrepareReconstructionBindings(')
    b=source.index('Status Sc6ReplayVfxState::ApplyFreshComponentValues(',a)
    (tmp_path/'reconstruction_inventory.inl').write_text(source[a:b],encoding='utf-8')
    shape=source.index('                if (!live || gpu_count')
    (tmp_path/'emitter_shape.inl').write_text(source[shape:source.index(';',shape)+1],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_reconstruction_inventory_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:slot.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'slot.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_render_owner_projection_and_native_membership(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    directory=module.ROOT/'HorseMod/horselib/deterministic'
    source=(directory/'Sc6ReplayParticleCopy.Lighting.inl').read_text(encoding='utf-8')
    a=source.index('bool Sc6ReplayParticleCopy::PrepareParticleRenderOwners(')
    b=source.index('bool Sc6ReplayParticleCopy::PendingStageTargetBinding(',a)
    fresh=source[source.index('bool Sc6ReplayParticleCopy::PrepareFreshTraceRenderOwners('):source.index('bool Sc6ReplayParticleCopy::PendingTraceTargetBinding(')]
    (tmp_path/'particle_render_owner.inl').write_text(fresh+source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_particle_render_owner_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{directory}" "{fixture}" /Fe:owner.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_proxy_handoff_preserves_pending_emitter_work(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleComponentOwner.hpp').read_text(encoding='utf-8')
    declarations=source[source.index('    enum class Phase'):source.index('    static bool NativeRegisterGpuRender')]
    declarations+=source[source.index('    static bool QueueGpuRenderRegistration'):source.index('    static bool NativeRemoveRenderState')]
    methods=source[source.index('    static bool RemoveInactiveRenderState'):source.index('    static bool NativeAdd')]
    (tmp_path/'particle_proxy_handoff.inl').write_text(declarations+methods,encoding='utf-8')
    fixture=module.ROOT/'tools/replay_particle_proxy_handoff_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:owner.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_host_fresh_scheduler_transposition_preserves_logical_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.ParticleOwners.inl').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayHost::TransposeFreshParticleScheduler()')
    b=source.index('Status Sc6ReplayHost::RetirePreparedFreshParticles(',a)
    (tmp_path/'fresh_scheduler_transposition.inl').write_text(source[a:b],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_fresh_scheduler_transposition_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:owner.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_fresh_coordinate_preparation_has_no_b_render_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6ReplayVfxState::PrepareCoordinateReconstruction(')
    b=source.index('void Sc6ReplayVfxState::PreparedManager::Clear(',a)
    (tmp_path/'fresh_coordinates.inl').write_text(source[a:b],encoding='utf-8')
    render=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text(encoding='utf-8')
    start=render.index('bool Sc6ReplayParticleCopy::CoordinatesBound(')
    end=render.index('bool Sc6ReplayParticleCopy::PrepareCoordinates(',start)
    rebuild=render[render.index('bool Sc6ReplayParticleCopy::RebuildCoordinates('):render.index('bool Sc6ReplayParticleCopy::CommitInstalled(')]
    (tmp_path/'fresh_coordinates_bound.inl').write_text(render[start:end]+rebuild,encoding='utf-8')
    fixture=module.ROOT/'tools/replay_fresh_coordinates_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:owner.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_fresh_owner_release_requires_settlement_and_completed_render_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.ParticleOwners.inl').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplayHost::SettleFreshParticleOwners()')
    (tmp_path/'fresh_owner_release.inl').write_text(source[start:],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_fresh_owner_release_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:owner.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'owner.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_hud_fresh_damage_owner_undo(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHudState.ActiveDamage.inl").read_text(encoding="utf-8")
    methods=source[source.index("bool ValidateDiscardedDamage("):]
    (tmp_path / "hud_fresh_damage_methods.inl").write_text(methods,encoding="utf-8")
    fixture=module.ROOT / "tools/replay_hud_fresh_damage_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:hud.exe /Fo:fixture.obj\n',encoding="utf-8")
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "hud.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_multi_gpu_factory_selects_and_retires_each_ordinal(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplayVfxState::ConstructFreshParticleOwner(')
    end=source.index('void Sc6ReplayVfxState::DescribeCapturedReferences(',start)
    (tmp_path/'multi_gpu_factory.inl').write_text(source[start:end],encoding='utf-8')
    start=source.index('bool Sc6ReplayVfxState::PreparedEmitterSet::FreshComponentDeath(')
    end=source.index('Status Sc6ReplayVfxState::PreparedEmitterSet::SettleExecution(',start)
    (tmp_path/'multi_gpu_settlement.inl').write_text(source[start:end],encoding='utf-8')
    start=source.index('                const auto gpu=reconstructed->FreshComponentRoots(')
    end=source.index('return Status::failure(FailureCode::GenerationMismatch);',start)+len('return Status::failure(FailureCode::GenerationMismatch);')
    (tmp_path/'cpu_component_manager.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_multi_gpu_factory_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:factory.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'factory.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_gpu_parameter_admission_requires_retained_b_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text(encoding='utf-8')
    start=source.index('bool Sc6ReplayParticleCopy::AdmitRenderReconstruction(')
    end=source.index('bool Sc6ReplayParticleCopy::SelectionMatches(',start)
    (tmp_path/'gpu_parameter_admission.inl').write_text(source[start:end],encoding='utf-8')
    host=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl').read_text(encoding='utf-8')
    start=host.index('    const bool pending_birth_fields =')
    (tmp_path/'gpu_parameter_deferred.inl').write_text(host[start:host.index(';',start)+1],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_gpu_parameter_admission_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod/horselib"}" "{fixture}" /Fe:admission.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'admission.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_fresh_static_field_keeps_b_and_borrowed_asset(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text(encoding='utf-8')
    start=source.index('bool Sc6ReplayParticleCopy::CaptureLocalFields(')
    end=source.index('bool Sc6ReplayParticleCopy::AdmitRenderReconstruction(',start)
    (tmp_path/'fresh_static_field.inl').write_text(source[start:end],encoding='utf-8')
    projection=source.index('    for(std::size_t i=0;i<local_fields_a_.count;++i)',source.index('    coordinates_ = std::move(rows);'))
    projection_end=source.index('    // The host prepares',projection)
    (tmp_path/'fresh_static_projection.inl').write_text(source[projection:projection_end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_fresh_static_field_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod/horselib"}" "{fixture}" /Fe:field.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'field.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_factory_journals_partial_native_acquisition(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    fixture=module.ROOT/'tools/replay_trace_child_factory_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" "{fixture}" /Fe:factory.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'factory.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_scheduler_binding_uses_retained_source_and_native_owner(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshChild.inl').read_text(encoding='utf-8')
    start=source.index('Status ValidateFreshChildExecution(')
    end=source.index('bool FreshChildDormantRenderSource(',start)
    (tmp_path/'trace_child_mesh_binding.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_mesh_binding_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT}" "{fixture}" /Fe:binding.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'binding.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_survivor_view_never_installs_or_reads_dead_child(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    state=(root/'Sc6ReplayTraceState.hpp').read_text(encoding='utf-8')
    projection=(root/'Sc6ReplayTraceProjection.inl').read_text(encoding='utf-8')
    equal=state[state.index('    static bool Equal('):state.index('    static bool Writable(')]
    accounting=state[state.index('    std::size_t owned_bytes() const'):state.index('    Status Capture(')]
    validation=state[state.index('    Status ValidateBindings() const'):state.index('    static bool MaterialRangeReadable(')]
    install=state[state.index('    Status Install() const'):state.index('\n};',state.index('    Status Install() const'))]
    view=projection[projection.index('Status CreateSurvivingOwnerView('):]
    (tmp_path/'trace_survivor_methods.inl').write_text(equal+accounting+validation+install+view,encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_survivor_view_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:view.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'view.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_preparation_waits_for_native_work_and_gpu_before_projection(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.ParticleOwners.inl').read_text(encoding='utf-8')
    prefix=source[:source.index('    const auto live=')]
    (tmp_path/'trace_host_preparation_order.inl').write_text(prefix+'    return Status::success();\n}\n',encoding='utf-8')
    render=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshRender.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_fresh_render_queue.inl').write_text(render[:render.index('static bool RunFreshChildRenderWork')],encoding='utf-8')
    (tmp_path/'trace_fresh_render_drain.inl').write_text(render[render.index('Status DrainFreshChildRenderWork'):],encoding='utf-8')
    fresh=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshChild.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_fresh_dormant_source.inl').write_text(fresh[fresh.index('bool FreshChildDormantRenderSource'):fresh.index('Status ConstructFreshChild')],encoding='utf-8')
    driver=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl').read_text(encoding='utf-8')
    start=driver.index('            const auto owners=PrepareFreshParticleOwners();')
    (tmp_path/'trace_preparation_driver_gate.inl').write_text(driver[start:driver.index('            if(!transaction.particle_bindings.empty()',start)],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_preparation_order_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:preparation.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'preparation.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_projection_uses_typed_native_bindings(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceProjection.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_child_projection_binding.inl').write_text(source[:source.index('Status ProjectFreshChildren(')],encoding='utf-8')
    animation=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshAnimation.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_child_animation_owner.inl').write_text(animation[:animation.index('struct ChildAnimationRows')],encoding='utf-8')
    start=source.index('        const auto project=')
    end=source.index('        for(std::size_t i=0;i<bindings_.size();',start)
    (tmp_path/'trace_child_project_image.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_projection_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:projection.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'projection.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_animation_restores_each_retained_destination(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshAnimation.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_child_animation_write.inl').write_text(source[source.index('struct ChildAnimationRows'):source.index('Status PrepareFreshChildAnimation(')],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_animation_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:animation.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'animation.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_history_preserves_fresh_bindings_and_complete_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    source=(include/'Sc6ReplayTraceReconstruction.inl').read_text(encoding='utf-8')
    start=source.index('const std::byte* RetainedSpan(')
    end=source.index('template<class Accept> Status VisitHistoricalChildSources(',start)
    (tmp_path/'trace_retained_spans.inl').write_text(source[start:end],encoding='utf-8')
    (tmp_path/'trace_fresh_history.inl').write_text((include/'Sc6ReplayTraceFreshHistory.inl').read_text(encoding='utf-8'),encoding='utf-8')
    retirement=(include/'Sc6ReplayTraceRetirement.inl').read_text(encoding='utf-8')
    overlap=retirement[retirement.index('    static bool StorageOverlaps('):retirement.index('    bool Contains(')]
    storage=retirement[retirement.index('    Status CheckChildStorageAndLifecycle('):retirement.index('        const auto cached=At<Ref>(state.ref.state,0xa0);')]
    (tmp_path/'trace_child_storage_alias.inl').write_text(overlap+storage+'        return Status::success();\n    }\n',encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_history_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{include}" "{fixture}" /Fe:history.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'history.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_trace_child_cleanup_waits_for_new_gpu_completion(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.ParticleOwners.inl').read_text(encoding='utf-8')
    start=source.index('Status Sc6ReplayHost::RetirePreparedFreshParticles(')
    end=source.index('Status Sc6ReplayHost::BeginFreshParticleRenderOwners()',start)
    body=source[start:end]
    prefix=body[:body.index('    using Native=')]
    queue=body[body.index('    bool queued=false;'):body.index('    for(auto& pointer:operation.fresh_particles)')]
    finish=body[body.index('    if(queued) {'):]
    finish=finish[:finish.index('    for(auto& pointer:operation.fresh_particles)')]
    (tmp_path/'trace_child_cleanup.inl').write_text(prefix+queue+finish+'    return Status::success();\n}\n',encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_cleanup_selftest.cpp'
    release=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceFreshChild.inl').read_text(encoding='utf-8')
    (tmp_path/'trace_child_release.inl').write_text(release[release.index('Status ReleaseFreshChildOwnership('):],encoding='utf-8')
    include=module.ROOT/'HorseMod/horselib/deterministic'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{include}" "{fixture}" /Fe:cleanup.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'cleanup.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_historical_trace_child_source_uses_retained_bytes(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayTraceReconstruction.inl'
    (tmp_path/'trace_child_source.inl').write_text(source.read_text(encoding='utf-8'),encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_child_source_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:source.exe /Fo:fixture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'source.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_pending_trace_target_requires_owned_native_reconstruction(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.Lighting.inl").read_text(encoding="utf-8")
    name="PendingTraceTargetBinding" if "::PendingTraceTargetBinding(" in source else "TraceRenderBinding"
    start=source.index("bool Sc6ReplayParticleCopy::"+name+"(")
    end=source.index("\n}",start)+2
    method=source[start:end].replace("Sc6ReplayParticleCopy::", "").replace(name,"PendingTraceTargetBinding")
    (tmp_path / "pending_trace_method.inl").write_text(method,encoding="utf-8")
    fixture=module.ROOT / "tools/replay_pending_trace_target_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:pending-trace.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path / "pending-trace.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


@pytest.mark.native_contract
def test_rolling_release_enables_real_continuation_measurement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "tools/replay_qualification_mod/ReplayRolling.inl").read_text(encoding="utf-8")
    start=source.index("        interior_completed_=true;")
    (tmp_path / "rolling_resume.inl").write_text(source[start:source.index("        if(!resume",start)],encoding="utf-8")
    source=(module.ROOT / "tools/replay_qualification_mod/ReplayQualificationMod.cpp").read_text(encoding="utf-8")
    start=source.index("        if (world_pause_completed_ && !world_resume_measured_)")
    (tmp_path / "rolling_measure.inl").write_text(source[start:source.index("        if (trajectory_samples_ == 0)",start)],encoding="utf-8")
    fixture=module.ROOT / "tools/replay_rolling_resume_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:resume.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path / "resume.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


def test_rolling_independent_traversals_reject_pose_and_lifecycle_drift():
    import re
    from replay_test import compare_rolling_traversals
    def segment(identity,generation,first,last):
        rows=[]
        for tick in range(first,last+1):
            rows.append(f'[ReplayQualification] boundary ordinal={tick} phase=input_cache_publication sample_version=1 frame={tick-1} x=0')
            if tick==213:
                rows.append(f'[ReplayQualification] particle seed run_id={identity} component=abc emitter=def frame=212 seed=17 game_thread=true')
            rows.append(f'[ReplayQualification] particle receiver run_id={identity} tick={tick} ordinal=0 asset=abc routes=1 events=0,1,0,0,0 event_hash=123 before_count=1 after_count=2 before_hash=456 after_hash=789 rng_before=12 rng_after=34 delta=3c888889 native_returned=true game_thread=true read_only=true')
            rows.append(f'[ReplayQualification] particle receivers completed run_id={identity} tick={tick} generation={generation} calls=1 schema=1 read_only=true')
            rows.append(f'[ReplayQualification] native pose publication run_id={identity} tick={tick} generation={generation} ordinal=0 player=0 bones=2 mapping=3 hash={tick}')
            rows.append(f'[ReplayQualification] native pose completed run_id={identity} tick={tick} generation={generation} evaluations=1')
            rows.append(f'[ReplayQualification] trajectory ordinal={tick} phase=engine_post sample_version=1 frame={tick} x=0')
        return '\n'.join(rows)+'\n'
    native=segment('n',0,211,338)
    candidate=segment('c',1,211,218)+segment('c',2,212,338)
    result=compare_rolling_traversals(native,candidate,'n','c',2)
    assert result['pose_traversals']==135 and result['lifecycle_events']==2
    assert result['receiver_traversals']==135 and result['receiver_calls']==135
    revised=native.replace('hash=213','hash=999')
    mixed=segment('c',1,211,218)+segment('c',2,212,338).replace('hash=213','hash=999')
    assert compare_rolling_traversals(native,mixed,'n','c',2,ordinals=[1])['pose_traversals']==8
    assert compare_rolling_traversals(revised,mixed,'n','c',2,ordinals=[2])['pose_traversals']==127
    with pytest.raises(RuntimeError):compare_rolling_traversals(revised,mixed,'n','c',2)

    shift=lambda text:re.sub(r'(tick|frame)=(\d+)',lambda m:m[1]+'='+str(int(m[2])+122),text)
    assert compare_rolling_traversals(shift(native),shift(candidate),'n','c',2,first=332)==result
    with pytest.raises(RuntimeError):compare_rolling_traversals(shift(native),shift(candidate),'n','c',2)
    for broken in (candidate.replace('hash=213','hash=999',1),candidate.replace('seed=17','seed=18',1),
                   candidate.replace('tick=338 generation=2 evaluations=1','tick=338 generation=2 evaluations=2'),
                   candidate.replace('frame=210 x=0','frame=209 x=0',1),
                   candidate.replace('game_thread=true','game_thread=false',1)):
        with pytest.raises(RuntimeError):compare_rolling_traversals(native,broken,'n','c',2)
    # A matching pose/lifecycle stream cannot hide a skipped, duplicated or
    # wrong receiver result. These rows come from actual native completion.
    for broken in (candidate.replace('after_hash=789','after_hash=999',1),
                   candidate.replace('rng_after=34','rng_after=35',1),
                   candidate.replace('particle receiver run_id=c','missing receiver run_id=c',1),
                   candidate.replace('calls=1 schema=1','calls=2 schema=1',1),
                   candidate.replace('ordinal=0 asset=abc','ordinal=1 asset=abc',1),
                   candidate.replace('events=0,1,0,0,0','events=0,2,0,0,0',1),
                   candidate.replace('native_returned=true','native_returned=false',1),
                   candidate.replace('particle receivers completed','missing receivers completed',1)):
        with pytest.raises(RuntimeError):compare_rolling_traversals(native,broken,'n','c',2)
    import re
    stripped=lambda text:re.sub(r'^.*particle receiver[^\n]*\n','',text,flags=re.M)
    with pytest.raises(RuntimeError):compare_rolling_traversals(stripped(native),stripped(candidate),'n','c',2)


@pytest.mark.native_contract
def test_rolling_coherence_deduplicates_repeated_ticks_within_existing_budget(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "tools/replay_qualification_mod/ReplayPresentationObserver.hpp").read_text(encoding="utf-8")
    start=source.index("        if(!coherence_ || !accepting_ || error_) return;")
    (tmp_path / "coherence_admission.inl").write_text(source[start:source.index("        if(!coherence_thread_)",start)],encoding="utf-8")
    fixture=module.ROOT / "tools/replay_rolling_coherence_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:coherence.exe /Fo:fixture.obj\n')
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    result=subprocess.run([str(tmp_path / "coherence.exe")],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


def test_rolling_executor_revalidation_counts_actual_repeated_work(tmp_path):
    import replay_test as module
    p=tmp_path/'rolling.log'
    text=(module.ROOT/'tools/replay_rolling_executor_receipt.txt').read_text(encoding='utf-8')
    report={'run_id':'fixture','rolling_cycles':30,'probe_historical_restore':True,'historical_anchor_tick':210,'historical_advanced_tick':217}
    def check(body):
        p.write_text(body,encoding='utf-8');report['raw_log']={'path':str(p),'sha256':module.sha256_file(p)}
        return module.executor_proof(report,True)
    result=check(text)
    assert result['historical_rewind_ticks']==210 and result['historical_rewind_intervals']==210
    assert result['physical_ticks']==723 and result['physical_intervals']==719
    for broken in (text.replace('resimulated_intervals=210','resimulated_intervals=209'),
                   text.replace('completed_intervals=719','completed_intervals=718'),
                   text.replace('ordinal=28 T=244 A=237','ordinal=28 T=244 A=236')):
        with pytest.raises(RuntimeError):check(broken)


def test_rolling_hud_requires_every_traversal_and_ordered_clocks():
    import re
    from replay_test import compare_rolling_hud
    def segment(identity,generation,first,last):
        rows=[]
        for tick in range(first,last+1):
            rows.extend([f'[ReplayQualification] HUD state tick={tick} p1=1 p2=2 combo1=3 combo2=4 timer=0 damage=1 types=1 pool=10 active=1 type_pool=10 type_active=0 type_players=abc read_only=true',
                         f'[ReplayQualification] HUD clock tick={tick} widget=DmgValueEff_C player=0 delta=0.016666668 slate_delta={generation}.2 before=0.1 after=0.2 read_only=true',
                         f'[ReplayQualification] native pose completed run_id={identity} tick={tick} generation={generation} evaluations=1'])
        return '\n'.join(rows)+'\n'
    native=segment('n',0,211,338)
    candidate=segment('c',1,211,218)+segment('c',2,212,338)
    result=compare_rolling_hud(native,candidate,'n','c',2)
    assert result['hud_traversals']==135 and result['active_player_updates']==135
    mixed=segment('c',1,211,218)+segment('c',2,212,338).replace('p1=1','p1=9')
    revised=native.replace('p1=1','p1=9')
    assert compare_rolling_hud(native,mixed,'n','c',2,ordinals=[1])['hud_traversals']==8
    assert compare_rolling_hud(revised,mixed,'n','c',2,ordinals=[2])['hud_traversals']==127
    with pytest.raises(RuntimeError):compare_rolling_hud(revised,mixed,'n','c',2)

    shift=lambda text:re.sub(r'tick=(\d+)',lambda m:'tick='+str(int(m[1])+122),text)
    assert compare_rolling_hud(shift(native),shift(candidate),'n','c',2,first=332)==result
    with pytest.raises(RuntimeError):compare_rolling_hud(shift(native),shift(candidate),'n','c',2)
    for broken in (candidate.replace('p2=2','p2=9',1),candidate.replace('after=0.2','after=0.3',1),
                   candidate.replace('HUD state tick=213','HUD state tick=214',1),
                   candidate.replace('HUD clock tick=213','missing clock tick=213',1),
                   candidate.replace('run_id=c tick=213','run_id=alien tick=213',1)):
        with pytest.raises(RuntimeError): compare_rolling_hud(native,broken,'n','c',2)


@pytest.mark.native_contract
def test_rolling_600_observers_cover_continuous_native_window(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'tools/replay_qualification_mod/ReplayPresentationObserver.hpp').read_text(encoding='utf-8')
    start=source.index('    bool ObserveTick(');end=source.index('    static constexpr std::size_t reserved_bytes',start)
    hud=(module.ROOT/'tools/replay_qualification_mod/ReplayBoundaryObserver.hpp').read_text(encoding='utf-8')
    hstart=hud.index('        const bool target_window=');hend=hud.index('        const bool private_hud_window',hstart)
    fixture=tmp_path/'coverage.cpp'
    import re
    life=source[source.index('    static void* CreateGpuEmitter('):source.index('    static void InitializeEmitter(')]
    selectors=[re.search(r'if\((self.accepting_ .*?)\) \{',life).group(1),re.search(r'const bool selected=(.*?);',life).group(1)]
    init=source[source.index('    static void InitializeEmitter('):]
    selectors.append(re.search(r'if\((tick>=170.*?)\)\n',init).group(1).replace('self->','self.'))
    lifecycle=''.join('bool L'+str(i)+'(unsigned tick) {auto& self=*this;return '+x+';}\n' for i,x in enumerate(selectors))
    fixture.write_text('#include <cstdint>\n#include <cassert>\n#include "ReplayIndexCompletion.hpp"\nstruct Observer { bool accepting_{true};bool index_sequence_{};unsigned extra_first_{816},extra_last_{936};static constexpr unsigned observation_last_tick=340;\n'+source[start:end]+lifecycle+'};\nbool Hud(unsigned tick,unsigned target) { struct {bool index_sequence_{};unsigned hud_target_;} self{false,target};\n'+hud[hstart:hend]+'return target_window;}\nint main(){Observer o;for(unsigned t=210;t<=936;++t){assert(o.ObserveTick(t));assert(Hud(t,816));assert(o.L0(t));assert(o.L1(t));assert(o.L2(t));}assert(!o.ObserveTick(937));assert(!Hud(937,816));assert(!Hud(450,2510));}\n',encoding='utf-8')
    batch=tmp_path/'compile.cmd'
    # Relative source spelling keeps assert's __FILE__ stable across isolated
    # execution directories without normalizing away compiler-observed bytes.
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{module.ROOT / "tools/replay_qualification_mod"}" "{fixture.name}" /Fe:coverage.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'coverage.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_emitter_set_admits_only_witnessed_partial_gpu_death(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayVfxState.cpp').read_text(encoding='utf-8')
    start=source.index('    const auto destroyed_member =');end=source.index('    // A child validates',start)
    (tmp_path/'member.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_partial_gpu_member_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:member.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'member.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_production_hud_empty_type_backing(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHudState.ActiveTypes.inl').read_text(encoding='utf-8')
    methods=source[source.index('Status PrepareTypePlayers('):source.index('bool FinishReconstructedTypePlayer(')]
    methods+=source[source.index('bool PublishTypeBacking('):]
    (tmp_path/'hud_empty_type_methods.inl').write_text(methods,encoding='utf-8')
    fixture=module.ROOT/'tools/replay_hud_empty_type_backing_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:hud.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'hud.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_unstarted_fresh_coordinates_do_not_require_native_death(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text(encoding='utf-8')
    methods=source[source.index('bool Sc6ReplayParticleCopy::SealFreshCoordinateRetirement('):source.index('bool Sc6ReplayParticleCopy::PrepareExecutionCoordinates(')]
    (tmp_path/'coordinate_retirement.inl').write_text(methods,encoding='utf-8')
    host=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Restore.inl').read_text(encoding='utf-8')
    start=host.index('    if (operation.phase == RestoreOperationPhase::Recovering)')
    end=host.index('    if (operation.phase == RestoreOperationPhase::Recovered)',start)
    (tmp_path/'coordinate_recovery_order.inl').write_text(host[start:end],encoding='utf-8')

    fixture=module.ROOT/'tools/replay_fresh_coordinate_retirement_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:coordinates.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'coordinates.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


def test_rolling_runner_stops_on_unrecoverable_native_undo(tmp_path):
    import re, textwrap
    from types import SimpleNamespace
    import replay_test as module
    source=(module.ROOT/'tools/deterministic_qualification/replay_control.py').read_text(encoding='utf-8')
    start=source.index('            def guard():')
    end=source.index('                if report["coherence_images_requested"]:',start)
    code=compile(textwrap.dedent(source[start:end]),'production_rolling_guard','exec')
    log=tmp_path/'native.log'
    scope=dict(re=re,run_id='candidate',args=SimpleNamespace(log=log),
        __package__='deterministic_qualification',diagnostic_log_state={},
        exit_witness=SimpleNamespace(exit_code=lambda:None),
        process=SimpleNamespace(memory_info=lambda:SimpleNamespace(rss=1,peak_wset=1)))
    exec(code,scope)
    for cycles,failure,raises in [(600,18,True),(0,18,False),(600,0,False)]:
        scope['report']={'rolling_cycles':cycles}
        log.write_text(f'[HorseMod] historical undo participant=fresh_coordinate_retirement code={failure}\n',encoding='utf-8')
        if raises:
            with pytest.raises(RuntimeError,match='rolling native undo failed'):scope['guard']()
        else:scope['guard']()

    scope['report']={'rolling_cycles':600}
    log.write_text('[HorseMod] restore preparation failed participant=fresh_particle_owners code=17 target=318 original=325 before_publication=true\n',encoding='utf-8')
    with pytest.raises(RuntimeError,match='rolling restore preparation failed'):scope['guard']()

    # A surface rejection can leave the native host safely held without an
    # observer terminal receipt. The rolling runner must stop at that first
    # rejection, rather than burn its timeout or label cleanup as B recovery.
    log.write_text('[HorseMod] replay surface command rejected command=0 tick=215 B_retained=true arm_budget=1\n',encoding='utf-8')
    with pytest.raises(RuntimeError,match='rolling surface command rejected'):scope['guard']()
    log.write_text('[HorseMod] historical commit retirement failed cursor=11 code=7 B_recovery_available=false\n',encoding='utf-8')
    with pytest.raises(RuntimeError,match='rolling commit retirement failed'):scope['guard']()
    log.write_text('[HorseMod] physics marker commit preflight rejected check=sap_owner code=7 B_retained=true commit_decided=false\n',encoding='utf-8')
    with pytest.raises(RuntimeError,match='rolling physics marker commit preflight failed'):scope['guard']()
    scope['report']={'rolling_cycles':0}
    scope['guard']()


@pytest.mark.native_contract
def test_dormant_trace_without_primitive_needs_no_quarantine_id(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayParticleCopy.cpp').read_text(encoding='utf-8')
    methods=source[source.index('bool Sc6ReplayParticleCopy::AppendQuarantinedTracePrimitive('):source.index('bool Sc6ReplayParticleCopy::PrepareGroundRenderOwner(')]
    (tmp_path/'trace_quarantine.inl').write_text(methods,encoding='utf-8')
    fixture=module.ROOT/'tools/replay_trace_quarantine_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:quarantine.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'quarantine.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_production_transient_capture_binding_envelope(tmp_path):
    import re, subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    source=(root/'Sc6CandidateCheckpointCapture.cpp').read_text(encoding='utf-8')
    a=source.index('Status Sc6CandidateCheckpointCapture::BindForCanonicalCapture(')
    b=source.index('Status Sc6CandidateCheckpointCapture::finish_transient_capture(',a)
    methods=source[a:b]
    marker='bool Sc6CandidateCheckpointCapture::RequiresCaptureBinding('
    if marker in source:
        a=source.index(marker)
        methods=source[a:source.index('Status Sc6CandidateCheckpointCapture::BindForCanonicalCapture(',a)]+methods
    a=source.index('Status Sc6CandidateCheckpointCapture::PrepareEnclosingWind(')
    methods+=source[a:source.index('std::size_t Sc6CandidateCheckpointCapture::transient_allocation_envelope_bytes()',a)]
    (tmp_path/'capture_binding_methods.inl').write_text(methods,encoding='utf-8')
    host=(root/'Sc6ReplayHost.Checkpoint.inl').read_text(encoding='utf-8')
    expression=re.search(r'if \(([^\n]+) > AdmissionRemaining\(&output\)\) \{\n        RC::Output.*?checkpoint capacity participant=transient_capture',host).group(1)
    (tmp_path/'capture_binding_admission.inl').write_text('return '+expression+';\n',encoding='utf-8')
    start=host.index('        const auto& operation = *historical_restore_;',host.index('std::size_t Sc6ReplayHost::AdmissionBytes'))
    (tmp_path/'complete_b_admission.inl').write_text(host[start:host.index('        add(operation.manager.',start)],encoding='utf-8')
    start=host.index('    add(Horse::GameImGui::PresentHook::replay_timeline_control_bytes());')
    start=host.index('\n',start)+1
    (tmp_path/'complete_b_checkpoint_charge.inl').write_text(host[start:host.index('    bool pending_counted',start)],encoding='utf-8')
    source=(root/'Sc6ReplayHost.cpp').read_text(encoding='utf-8')
    start=source.index('std::size_t Sc6ReplayHost::Checkpoint::owned_bytes()')
    (tmp_path/'complete_b_owned.inl').write_text(source[start:source.index('struct Sc6ReplayHost::HistoricalRestore',start)],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_capture_binding_envelope_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:capture.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'capture.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_rolling_callbacks_use_each_traversals_authored_revision():
    from replay_test import compare_execution_callback_segments
    original=[{'ordinal':str(i),'frame':str(i),'phase':'input','raw':'0'} for i in range(1,5)]
    revised=[dict(row,raw='1' if int(row['frame'])>=3 else '0') for row in original]
    candidate=original+revised[1:]
    assert compare_execution_callback_segments(original,candidate,4,segment_histories={0:original,1:revised})==7
    with pytest.raises(RuntimeError):compare_execution_callback_segments(revised,candidate,4)
    with pytest.raises(RuntimeError):compare_execution_callback_segments(original,candidate,4,segment_histories={1:revised})
    with pytest.raises(RuntimeError):compare_execution_callback_segments(original,candidate,4,segment_histories={0:original,1:original})


def test_rolling_schedule_wire_request_is_bounded_and_versioned():
    from deterministic_qualification.replay_control import rolling_correction_request,rolling_correction_groups,rolling_correction_prefix
    assert rolling_correction_request('')==(13,'')
    wire='217:0:0:169:2:0:0;218:1:0:170:1:1024:0'
    assert rolling_correction_request(wire)==(14,'rolling_corrections='+wire+'\n')
    grouped='217:0:0:169:1:1:0;217:0:0:170:2:0:2;218:1:0:171:1:3:0'
    assert rolling_correction_request(grouped)==(14,'rolling_corrections='+grouped+'\n')
    assert rolling_correction_groups(grouped)==[
        ['217:0:0:169:1:1:0','217:0:0:170:2:0:2'],['218:1:0:171:1:3:0']]
    assert rolling_correction_prefix(grouped,0)==''
    assert rolling_correction_prefix(grouped,1)=='217:0:0:169:1:1:0;217:0:0:170:2:0:2'
    assert rolling_correction_prefix(grouped,2)==grouped
    with pytest.raises(ValueError):rolling_correction_prefix(grouped,3)
    for invalid in ('217:0:0:169:0:0:0','217:0:0:169:2:0:4294967296',wire+';',
                    '217:0:0:169:2:0:0\nversion=13','217:0:0:169:2:0:0;216:1:0:170:2:0:0',
                    '217:0:0:169:1:1:0;217:1:0:170:2:0:2',
                    '217:0:0:169:1:1:0;217:0:0:169:2:0:2',
                    '217:0:0:169:1:1:0;217:0:1:170:2:0:2',
                    '217:0:0:169:1:1:0;218:0:0:170:2:0:2',
                    '217:2:0:169:1:1:0;218:1:0:170:2:0:2'):
        with pytest.raises(ValueError):rolling_correction_request(invalid)


def test_rolling_revision_assignment_requires_complete_ordered_history():
    from replay_test import rolling_revision_assignments
    wire='217:0:0:169:2:0:0;219:1:0:170:1:1024:0'
    assert rolling_revision_assignments(wire,4,210)=={0:0,1:1,2:1,3:2,4:2}
    grouped='217:0:0:169:1:1:0;217:0:0:170:2:0:2;219:1:0:171:1:3:0'
    assert rolling_revision_assignments(grouped,4,210)=={0:0,1:1,2:1,3:2,4:2}
    for invalid in (wire.replace('219:1','219:0'),wire.replace('217:0','216:0'),wire.replace('219:1','221:1')):
        with pytest.raises(ValueError):rolling_revision_assignments(invalid,4,210)


@pytest.mark.native_contract
def test_production_hud_fresh_type_owner_undo(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT / "HorseMod/horselib/deterministic/Sc6ReplayHudState.ActiveTypes.inl").read_text(encoding="utf-8")
    methods=source[source.index("Status UndoTypePlayers("):source.index("// Native completion can shrink")]
    (tmp_path / "hud_fresh_type_methods.inl").write_text(methods,encoding="utf-8")
    fixture=module.ROOT / "tools/replay_hud_fresh_type_selftest.cpp"
    batch=tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod"}" "{fixture}" /Fe:hud.exe /Fo:fixture.obj\n',encoding="utf-8")
    built=run_compile(["cmd","/d","/c",str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path / "hud.exe")],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr



@pytest.mark.native_contract
def test_hud_completion_handle_uses_native_callback_layout(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHudState.ActiveTypes.inl').read_text(encoding='utf-8')
    start=source.index('    const bool callback_valid=')
    end=source.index(';',start)+1
    predicate=source[start:end]
    fixture=tmp_path/'callback.cpp'
    fixture.write_text('#include <array>\n#include <cstdint>\n#include <cstring>\n#include <cassert>\ntemplate<class T>T& At(void* p,int offset){return *reinterpret_cast<T*>(static_cast<char*>(p)+offset);}\nstruct Effect{struct{int index=42,serial=91;}widget;};\nbool Valid(void* callback){std::uintptr_t base_=0x140000000;Effect e;\n'+predicate+'return callback_valid;}\nint main(){alignas(16) std::array<unsigned char,0x30> bytes{};void* p=bytes.data();\n At<std::uintptr_t>(p,0)=0x143285198;At<int>(p,8)=42;At<int>(p,12)=91;\n At<std::uintptr_t>(p,16)=0x1417f7df0;At<int>(p,24)=0;\n At<std::uint64_t>(p,0x28)=123;assert(Valid(p));\n At<std::uint64_t>(p,0x28)=0;At<std::uint64_t>(p,0x20)=123;assert(!Valid(p));\n At<std::uint64_t>(p,0x28)=123;At<int>(p,24)=4;assert(!Valid(p));\n At<int>(p,24)=0;At<int>(p,12)=92;assert(!Valid(p));\n}\n',encoding='utf-8')
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{fixture}" /Fe:callback.exe /Fo:fixture.obj\n',encoding='utf-8')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    ran=subprocess.run([str(tmp_path/'callback.exe')],capture_output=True,text=True,timeout=10)
    assert ran.returncode==0,ran.stdout+ran.stderr


@pytest.mark.native_contract
def test_particle_material_graph_captures_owned_bindings(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=r'''#include <array>
#include <cassert>
#include "ReplayParticleMaterialGraph.hpp"
using Horse::Deterministic::ReplayParticleMaterialGraph;
int main(){
 constexpr std::uintptr_t base=0x140000000;
 std::array<std::byte,0xad0> component{};std::array<std::byte,0x208> mid{};
 const auto owner=reinterpret_cast<std::uintptr_t>(component.data());
 const auto material=reinterpret_cast<std::uintptr_t>(mid.data());
 const auto put=[](void* p,std::size_t o,auto value){std::memcpy(static_cast<std::byte*>(p)+o,&value,sizeof(value));};
 std::uintptr_t roots[]{material,0};std::uintptr_t override_materials[]{material};
 put(mid.data(),0,base+0x391ee70);put(mid.data(),0x20,owner);put(mid.data(),0x78,std::uintptr_t{0x567800});
 put(component.data(),0xa98,roots);put(component.data(),0xaa0,2);put(component.data(),0xaa4,2);
 put(component.data(),0x810,override_materials);put(component.data(),0x818,1);put(component.data(),0x81c,1);
 const auto before=component;const auto material_before=mid;
 ReplayParticleMaterialGraph graph;
 assert(graph.Capture(base,owner));
 assert(graph.materials==1 && graph.arrays[0].slots[0].parent==0x567800);
 assert(graph.arrays[2].slots[0].object==material && !graph.arrays[0].slots[1].object);
 assert(component==before && mid==material_before);
 put(mid.data(),0x20,owner+8);assert(!graph.Capture(base,owner));put(mid.data(),0x20,owner);
 roots[1]=material;assert(!graph.Capture(base,owner));roots[1]=0;
 put(component.data(),0xaa4,33);assert(!graph.Capture(base,owner));put(component.data(),0xaa4,2);
 assert(graph.Capture(base,owner));
}
'''
    (tmp_path/'materials.cpp').write_text(source,encoding='utf-8')
    include=module.ROOT/'HorseMod/horselib/deterministic'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" materials.cpp /Fe:materials.exe /Fo:materials.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'materials.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_particle_material_realization_rebinds_and_keeps_b(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=r'''#include <array>
#include <cassert>
#include "ReplayParticleMaterialGraph.hpp"
using Horse::Deterministic::ReplayParticleMaterialGraph;
template<class T>void put(void* p,std::size_t o,T value){std::memcpy(static_cast<std::byte*>(p)+o,&value,sizeof(value));}
struct Native {
 std::uintptr_t base{},target{};unsigned creates{},parameters{},overrides{};bool fail{};
 std::array<std::uintptr_t,32> roots{},installed{};
 std::array<std::byte,0x208> mid{};std::array<std::byte,32> scalar{};
 bool Initialize(unsigned side,const std::uintptr_t* parents,int count,float){
  ++creates;assert(side==0&&count==2);const auto address=reinterpret_cast<std::uintptr_t>(mid.data());
  put(mid.data(),0,base+0x391ee70);put(mid.data(),0x20,target);put(mid.data(),0x78,parents[0]);roots[0]=address;
  put((void*)target,0xa98,roots.data());put((void*)target,0xaa0,count);put((void*)target,0xaa4,count);return !fail;
 }
 bool Scalar(std::uintptr_t object,std::uint64_t name,float value){
  ++parameters;assert(object==reinterpret_cast<std::uintptr_t>(mid.data()));
  put(scalar.data(),0,name);put(scalar.data(),8,value);put(mid.data(),0x98,scalar.data());put(mid.data(),0xa0,1);put(mid.data(),0xa4,1);return true;
 }
 bool Vector(std::uintptr_t,std::uint64_t,const float*){assert(false);return false;}
 bool Override(int slot,std::uintptr_t object){++overrides;installed[slot]=object;
  put((void*)target,0x810,installed.data());put((void*)target,0x818,slot+1);put((void*)target,0x81c,32);return true;}
};
int main(){
 constexpr std::uintptr_t base=0x140000000;
 std::array<std::byte,0xad0> b{},fresh{},partial{};std::array<std::byte,0x208> original_mid{};std::array<std::byte,32> scalar{};
 const auto source=reinterpret_cast<std::uintptr_t>(b.data()),mid=reinterpret_cast<std::uintptr_t>(original_mid.data());
 std::uintptr_t roots[]{mid,0},override_materials[]{mid};
 put(original_mid.data(),0,base+0x391ee70);put(original_mid.data(),0x20,source);put(original_mid.data(),0x78,std::uintptr_t{0x456700});
 put(scalar.data(),0,std::uint64_t{0x123});put(scalar.data(),8,0.625f);
 put(original_mid.data(),0x98,scalar.data());put(original_mid.data(),0xa0,1);put(original_mid.data(),0xa4,1);
 put(b.data(),0xa98,roots);put(b.data(),0xaa0,2);put(b.data(),0xaa4,2);
 put(b.data(),0x810,override_materials);put(b.data(),0x818,1);put(b.data(),0x81c,1);
 ReplayParticleMaterialGraph recipe;assert(recipe.Capture(base,source));
 const auto saved_b=b;const auto saved_mid=original_mid;const auto saved_scalar=scalar;
 Native native{base,reinterpret_cast<std::uintptr_t>(fresh.data())};
 assert(recipe.Realize(base,native.target,native));
 assert(native.creates==1&&native.parameters==1&&native.overrides==1&&native.installed[0]!=mid);
 ReplayParticleMaterialGraph observed;assert(observed.Capture(base,native.target)&&recipe.ValuesEqual(observed));
 assert(b==saved_b&&original_mid==saved_mid&&scalar==saved_scalar);
 Native failed{base,reinterpret_cast<std::uintptr_t>(partial.data())};failed.fail=true;
 assert(!recipe.Realize(base,failed.target,failed));assert(failed.creates==1&&!failed.parameters&&!failed.overrides);
 assert(b==saved_b&&original_mid==saved_mid&&scalar==saved_scalar);
 // Retrying a partial nonempty destination cannot create another native owner.
 failed.fail=false;assert(!recipe.Realize(base,failed.target,failed)&&failed.creates==1);
}
'''
    (tmp_path/'realize.cpp').write_text(source,encoding='utf-8')
    include=module.ROOT/'HorseMod/horselib/deterministic'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" realize.cpp /Fe:realize.exe /Fo:realize.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'realize.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_body_initial_kinematic_admission(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=r'''#include "ReplayGroundBodyAdmission.hpp"
#include <array>
#include <cstring>
#include <cassert>
using namespace Horse::Deterministic;
int main(){
 std::array<unsigned char,0x200> actor{};std::array<unsigned char,0x40> state{};
 const auto address=reinterpret_cast<std::uintptr_t>(actor.data()),kinematic=reinterpret_cast<std::uintptr_t>(state.data());
 std::memcpy(actor.data()+0x130,&kinematic,8);state[0x1f]=1;
 unsigned reads{};
 const auto read=[&](std::uintptr_t p,auto& value){++reads;
  if(!((p>=address&&p+sizeof(value)<=address+actor.size())||(p>=kinematic&&p+sizeof(value)<=kinematic+state.size())))return false;
  std::memcpy(&value,reinterpret_cast<void*>(p),sizeof(value));return true;};
 const auto before_actor=actor;const auto before_state=state;
 assert(ReplayGroundBodyAdmission::Flags(read,address,2,1)&&!reads);
 assert(ReplayGroundBodyAdmission::Flags(read,address,3,1));
 assert(actor==before_actor&&state==before_state);
 state[0x1c]=1;assert(!ReplayGroundBodyAdmission::Flags(read,address,3,1));state=before_state;
 state[0x1f]=0;assert(!ReplayGroundBodyAdmission::Flags(read,address,3,1));state=before_state;
 actor[0x17d]=0x80;assert(!ReplayGroundBodyAdmission::Flags(read,address,3,1));actor=before_actor;
 std::uintptr_t absent{};std::memcpy(actor.data()+0x130,&absent,8);assert(!ReplayGroundBodyAdmission::Flags(read,address,3,1));actor=before_actor;
 assert(!ReplayGroundBodyAdmission::Flags(read,address,3,5));
 assert(!ReplayGroundBodyAdmission::Flags(read,address,7,1));
 const auto unavailable=[](std::uintptr_t,auto&){return false;};assert(!ReplayGroundBodyAdmission::Flags(unavailable,address,3,1));
}
'''
    (tmp_path/'admission.cpp').write_text(source,encoding='utf-8')
    include=module.ROOT/'HorseMod/horselib/deterministic'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" admission.cpp /Fe:admission.exe /Fo:admission.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'admission.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_motion_probe_requires_completed_boundary_and_holds_partial_failure(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Rolling.inl').read_text(encoding='utf-8')
    start=source.index('bool Sc6ReplayHost::ProbeGroundMotion(')
    end=source.index('\n}\n',start)+3
    (tmp_path/'ground_motion_boundary.inl').write_text(source[start:end],encoding='utf-8')
    fixture=module.ROOT/'tools/replay_ground_motion_boundary_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:boundary.exe /Fo:boundary.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'boundary.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_motion_probe_uses_native_movement_and_reports_partial_failure(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    fixture=module.ROOT/'tools/replay_ground_motion_probe_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" "{fixture}" /Fe:motion.exe /Fo:motion.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'motion.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_lifecycle_observer_detects_shared_state_and_ignores_child_motion(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    fixture=module.ROOT/'tools/replay_ground_lifecycle_observer_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" "{fixture}" /Fe:lifecycle.exe /Fo:lifecycle.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'lifecycle.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_configuration_owns_native_nested_arrays(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    fixture=module.ROOT/'tools/replay_ground_configuration_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" "{fixture}" /Fe:configuration.exe /Fo:configuration.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'configuration.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_capture_uses_native_scene_transform(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    source=(include/'Sc6ReplayHost.GroundDebris.inl').read_text()
    header=(include/'Sc6ReplayGroundDebrisState.hpp').read_text()
    begin=header.index('    struct UpdateDiagnostic {')
    (tmp_path/'ground_update_diagnostic_fields.inl').write_text(header[begin:header.index('    // Current native callback',begin)])
    start=header.index('    struct Mesh {')
    (tmp_path/'ground_mesh_fields.inl').write_text(header[start:header.index('    std::span<const Mesh>',start)])
    for name,start,end in [('ground_root_fields.inl','    struct Root {','    std::uintptr_t base{},manager{},world{};'),
                           ('ground_dormant_tick.inl','    static bool indexed_live(','    template<class T> static bool write('),
                           ('ground_root_read.inl','    bool read_root(','    bool read_mesh('),
                           ('ground_mesh_read.inl','    bool read_mesh(','    bool same_root(')]:
        begin=source.index(start)
        (tmp_path/name).write_text(source[begin:source.index(end,begin)])
    begin=source.index('    bool same_root(')
    (tmp_path/'ground_same_root.inl').write_text(source[begin:source.index('    bool owned_component(',begin)])
    begin=source.index('Status Sc6ReplayGroundDebrisState::ValidateFrozen()')
    (tmp_path/'ground_validate_frozen.inl').write_text(source[begin:source.index('Status Sc6ReplayGroundDebrisState::PreparePhysics(',begin)])
    begin=source.index('Status Sc6ReplayGroundDebrisState::ValidateUpdate(')
    (tmp_path/'ground_validate_update.inl').write_text(source[begin:source.index('Status Sc6ReplayGroundDebrisState::Prepare(',begin)])
    fixture=module.ROOT/'tools/replay_ground_capture_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{include}" /I. "{fixture}" /Fe:capture.exe /Fo:capture.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'capture.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


def test_ground_admission_failure_stops_existing_control_guard(tmp_path):
    import ast
    import inspect
    from collections import defaultdict
    from deterministic_qualification import replay_control as control
    source=ast.parse(inspect.getsource(control.run_replay_control))
    guard=next(n for n in ast.walk(source) if isinstance(n,ast.FunctionDef) and n.name=='guard')
    native_log=tmp_path/'native.log'
    rejection='[HorseMod] ground update admission rejected check=update_physics_callbacks owner=0 tick=0 roots=0 meshes=0 before_native_application=true owners_retained=true\n'
    namespace={'__package__':control.__package__,'re':control.re,'run_id':'replay-current',
        'exit_witness':SimpleNamespace(exit_code=lambda:None),
        'process':SimpleNamespace(memory_info=lambda:SimpleNamespace(rss=1,peak_wset=1)),
        'args':SimpleNamespace(log=native_log),'report':defaultdict(bool),'diagnostic_log_state':{}}
    exec(compile(ast.Module(body=[guard],type_ignores=[]),'<production control guard>','exec'),namespace)
    native_log.write_text('run_id=replay-current\n'+rejection)
    with pytest.raises(RuntimeError,match='ground update admission.*update_physics_callbacks'):
        namespace['guard']()

    # Rotation/truncation must discard the former current-run attribution.
    native_log.write_text(rejection+'run_id=replay-old\n'+rejection)
    namespace['guard']()
    native_log.write_text('run_id=replay-current\nnormal progress\n')
    namespace['guard']()
    with native_log.open('a') as log:
        log.write(rejection+'normal progress\n'*5000)
    with pytest.raises(RuntimeError,match='ground update admission.*update_physics_callbacks'):
        namespace['guard']() # Even the rejection itself has aged out of the tail.

    native_log.write_text('run_id=replay-current\n')
    namespace['guard']()
    with native_log.open('a') as log: log.write(rejection[:-1])
    namespace['guard']() # Incomplete records wait for their terminating newline.
    with native_log.open('a') as log: log.write('\n')
    with pytest.raises(RuntimeError,match='ground update admission.*update_physics_callbacks'):
        namespace['guard']()
    native_log.write_text('run_id=replay-old\n'+rejection+'run_id=replay-current\nnormal progress\n')
    namespace['guard']() # No stale failure may terminate the current owned run.

    # The run marker is witnessed once, then ages out of the bounded tail.
    # An unlabelled native rejection must still stop the owned run promptly.
    with native_log.open('a') as log:
        log.write('normal progress\n' * 5000 + rejection)
    with pytest.raises(RuntimeError,match='ground update admission.*update_physics_callbacks'):
        namespace['guard']()


@pytest.mark.parametrize('version',(4,5))
def test_native_crt_schema_requires_boundary_and_setup_observations(version):
    from deterministic_qualification.replay_fidelity import validate_boundary_capture, validate_setup_capture
    fields='round cursor input_round input_time round_frame inputs p0_sim p0_step p0_render p1_sim p1_step p1_render p0_vital p1_vital p0_moves p1_moves manager_phase move_state round_state published_count published_pairs'
    common=' '.join(f'{k}=0' for k in fields.split())+' mt_cursor=0 mt_hash=0000000000000000 crt_state=12345678'
    if version==5:common+=' ground_roots=1 ground_next_id=2 ground_lifecycle=1234567812345678'
    boundary='boundaries started run_id=r native_frame=1\n[ReplayQualification] boundary ordinal=1 phase=actor_tail sample_version=4 frame=1 source_active=true '+common+'\nboundaries completed run_id=r observations=1 detached=true\n'
    setup='setup started run_id=r native_frame=1 source_equal=true\n[ReplayQualification] setup ordinal=1 phase=engine_post sample_version=4 frame=1 source_active=false '+common+'\nsetup completed run_id=r observations=1 native_frame=2\n'
    boundary=boundary.replace('sample_version=4',f'sample_version={version}')
    setup=setup.replace('sample_version=4',f'sample_version={version}')
    for raw,validate in [(boundary,validate_boundary_capture),(setup,validate_setup_capture)]:
        assert validate(raw,'r')[0]['crt_state']=='12345678'
        for invalid in [raw.replace(' crt_state=12345678',''),raw.replace('crt_state=12345678','crt_state=xyz'),raw.replace(' mt_hash=0000000000000000','')]:
            with pytest.raises(RuntimeError,match='missing or invalid'): validate(invalid,'r')
        if version==5:
            for invalid in [raw.replace(' ground_lifecycle=1234567812345678',''),raw.replace('ground_roots=1','ground_roots=-1'),raw.replace(' ground_next_id=2','')]:
                with pytest.raises(RuntimeError,match='missing or invalid'):validate(invalid,'r')


@pytest.mark.native_contract
def test_ucrt_draw_observer_reads_native_before_and_after(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    source=(include/'DeterministicHookSet.PresentationTerminals.inl').read_text()
    begin=source.index('int __cdecl DeterministicHookSet::UcrtRandDetour()')
    (tmp_path/'ucrt_draw_detour.inl').write_text(source[begin:source.index('void __cdecl DeterministicHookSet::UcrtSrandDetour',begin)])
    observer=(module.ROOT/'tools/replay_qualification_mod/ReplayTrajectoryObserver.hpp').read_text()
    begin=observer.index('bool ReadReplayNativeCrt(')
    (tmp_path/'independent_crt_observer.inl').write_text(observer[begin:observer.index('// UObject::IsReal',begin)])
    fixture=module.ROOT/'tools/replay_ucrt_draw_observer_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /MD /I"{include}" /I"{module.BUILD / "HorseMod/generated"}" /I. "{fixture}" "{include / "UcrtRandBroker.cpp"}" /Fe:draw.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'draw.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_update_precedes_native_application(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    header=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayGroundDebrisState.hpp').read_text()
    begin=header.index('    struct UpdateDiagnostic {')
    (tmp_path/'ground_update_diagnostic_fields.inl').write_text(header[begin:header.index('    // Current native callback',begin)])
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplayHost.Application.inl').read_text()
    (tmp_path/'ground_admit_application.inl').write_text(source[source.index('bool Sc6ReplayHost::AdmitGroundUpdate('):source.index('void Sc6ReplayHost::TickApplication(')])
    (tmp_path/'ground_tick_application.inl').write_text(source[source.index('void Sc6ReplayHost::TickApplication('):])
    fixture=module.ROOT/'tools/replay_ground_update_boundary_selftest.cpp'
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:update.exe /Fo:update.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'update.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.mark.native_contract
def test_ground_cold_root_rejects_publication_and_incomplete_retirement(tmp_path):
    import subprocess
    import replay_test as module
    from deterministic_iteration import VCVARS
    include=module.ROOT/'HorseMod/horselib/deterministic'
    header=(include/'Sc6ReplayGroundDebrisState.hpp').read_text()
    start=header.index('    struct ColdRoot {')
    (tmp_path/'ground_cold_root_fields.inl').write_text(header[start:header.index('    std::size_t root_count()',start)])
    source=(include/'Sc6ReplayHost.GroundDebris.inl').read_text()
    for name,start,end in [('ground_cold_native.inl','struct GroundColdNative {','Sc6ReplayGroundDebrisState::ColdRoot::~ColdRoot()'),
                           ('ground_cold_destructor.inl','Sc6ReplayGroundDebrisState::ColdRoot::~ColdRoot()','std::size_t Sc6ReplayGroundDebrisState::root_count()'),
                           ('ground_cold_release.inl','Status Sc6ReplayGroundDebrisState::ReleaseColdRoot(','Sc6ReplayGroundDebrisState::~Sc6ReplayGroundDebrisState()')]:
        begin=source.index(start);piece=source[begin:source.index(end,begin)]
        if name=='ground_cold_native.inl':piece=piece.rsplit('}',1)[0]
        (tmp_path/name).write_text(piece)
    fixture=module.ROOT/'tools/replay_ground_cold_root_selftest.cpp'
    graph=(include/'Sc6ReplayGroundColdGraph.inl').read_text()
    begin=graph.index('    static bool ChildPrivate(')
    (tmp_path/'ground_graph_private.inl').write_text(graph[begin:graph.index('\n};\n}',begin)])
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:root.exe /Fo:root.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    checked=subprocess.run([str(tmp_path/'root.exe')],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,checked.stdout+checked.stderr


@pytest.fixture(scope='session')
def component_dispatch_executable(tmp_path_factory):
    """One compile per configuration; no execution results are reused."""
    import replay_test as module
    from deterministic_iteration import VCVARS
    tmp_path = tmp_path_factory.mktemp('component-dispatch-build')
    source=(module.ROOT/'HorseMod/horselib/deterministic/Sc6ReplaySchedulerState.cpp').read_text(encoding='utf-8')
    helper=''
    if 'bool AdmitComponentTickConsumer(' in source:
        a=source.index('bool AdmitComponentTickConsumer(')
        helper=source[a:source.index('// Native map lookup',a)]
    (tmp_path/'scheduler_consumer_helper.inl').write_text(helper,encoding='utf-8')
    for name,start,end in [('capture','Status Sc6ReplaySchedulerState::CaptureTick(', 'Status Sc6ReplaySchedulerState::CaptureUnchecked('),('validate','Status Sc6ReplaySchedulerState::ValidateBindings(', 'Status Sc6ReplaySchedulerState::ReadPrerequisiteConsumer(')]:
        a=source.index(start)
        (tmp_path/f'scheduler_consumer_{name}.inl').write_text(source[a:source.index(end,a)],encoding='utf-8')
    include=module.ROOT/'HorseMod/horselib/deterministic'
    source=(include/'Sc6ReplayTaskGroup.cpp').read_text(encoding='utf-8')
    for name,start,end in [
        ('pump_depth','struct PumpDepth','\n}\n\nbool Sc6ReplayTaskGroup::ParallelTasks'),
        ('consumer_request','bool Sc6ReplayTaskGroup::RequestConsumerResume(', '\nSc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::ResumeConsumerTask('),
        ('consumer_resume','Sc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::ResumeConsumerTask(', '\nvoid Sc6ReplayTaskGroup::ReleaseEvent('),
        ('pump','void Sc6ReplayTaskGroup::PumpOne(', '\nSc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::DispatchTask('),
        ('dispatch','Sc6ReplayTaskGroup::DispatchOutcome Sc6ReplayTaskGroup::DispatchTask(', '\n#include "Sc6ReplayTaskGroup.Resume.inl"')]:
        a=source.index(start)
        (tmp_path/f'component_{name}.inl').write_text(source[a:source.index(end,a)],encoding='utf-8')
    fixture = module.ROOT / 'tools/replay_component_dispatch_selftest.cpp'
    executables = {}
    def compile_variant(fastfail=False, collision=False):
        key = (fastfail, collision)
        if key not in executables:
            name = 'collision' if collision else 'terminal' if fastfail else 'dispatch'
            flags = '/DREPLAY_COMPONENT_NATIVE_FASTFAIL ' if fastfail else ''
            if collision:
                import pefile
                with pefile.PE(str(module.GAME_ROOT / 'SoulcaliburVI.exe'), fast_load=True) as pe:
                    shader = pe.get_data(0x204cf60, 704)
                assert len(shader) == 704
                (tmp_path/'component_collision_shader.inl').write_text(
                    'static constexpr unsigned char collision_shader[]={' + ','.join(map(str, shader)) + '};')
                domain = (module.ROOT/'tools/replay_physics_markers_selftest.inl').read_text(encoding='utf-8')
                (tmp_path/'component_collision_domain.inl').write_text(
                    domain[domain.index('struct GroundCollisionFixture {'):domain.index('// Exercise synchronous native island retirement')],
                    encoding='utf-8')
                flags += f'/DREPLAY_COMPONENT_COLLISION_BOUNDARY /I"{include.parent}" '
            batch = tmp_path / f'compile-{name}.cmd'
            batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc {flags}/I. /I"{include}" "{fixture}" /Fe:{name}.exe /Fo:{name}.obj\n', encoding='utf-8')
            built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path,
                                capture_output=True, text=True, timeout=60)
            assert built.returncode == 0, built.stdout + built.stderr
            executables[key] = tmp_path / f'{name}.exe'
        return executables[key]
    return compile_variant


@pytest.mark.native_contract
def test_physics_scene_inventory_detects_changed_unrelated_body(tmp_path, component_dispatch_executable):
    """Production locked scene scan emits a complete diagnostic census."""
    import subprocess
    checked = subprocess.run([str(component_dispatch_executable(collision=True)), 'collision-inventory-change'],
                             cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, f'child exit={checked.returncode}\n' + checked.stdout + checked.stderr
    assert 'complete scene inventory detects changed, added and removed unrelated bodies' in checked.stdout


@pytest.mark.native_contract
def test_physics_dispatch_rejects_free_body_changed_after_entry(tmp_path, component_dispatch_executable):
    """Production collision admission + owned dispatch; no native lease/recovery grant."""
    import subprocess
    checked = subprocess.run([str(component_dispatch_executable(collision=True)), 'collision-changed-free-body'],
                             cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, f'child exit={checked.returncode}\n' + checked.stdout + checked.stderr
    assert 'collision consumer retained before native entry; B recovery unproven' in checked.stdout


@pytest.mark.native_contract
def test_scheduler_rejects_uncaptured_niagara_consumers(tmp_path, component_dispatch_executable):
    import subprocess
    checked = subprocess.run([str(component_dispatch_executable()), 'scheduler'], cwd=tmp_path,
                             capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert 'scheduler Niagara capture and changed-dispatch admission PASS' in checked.stdout



@pytest.mark.native_contract
def test_component_dispatch_revalidates_admitted_consumer(tmp_path, component_dispatch_executable):
    """G1 terminal enforcement only. Native probes do not grant ownership or recovery."""
    import subprocess
    executable = component_dispatch_executable()
    checked=subprocess.run([str(executable)],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert checked.returncode==0,f'child exit={checked.returncode}\n'+checked.stdout+checked.stderr
    assert 'terminal rejection before native entry' in checked.stdout
    import json
    failure=json.loads((tmp_path/'consumer_failure.json').read_text())
    assert failure['site']==2 and failure['reason']==4 and failure['native_started']==0
    (tmp_path/'consumer_failure.json').unlink()
    for mode in ('physics','physics-active-vehicle','physics-null','physics-vehicle'):
        physics=subprocess.run([str(executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert physics.returncode==0,f'{mode} child exit={physics.returncode}\n'+physics.stdout+physics.stderr
        if mode in ('physics-null','physics-vehicle'):
            assert (tmp_path/'consumer_failure.json').read_bytes()==b''
        else:
            assert 'physics callback rejected before native wrapper' in physics.stdout
            failure=json.loads((tmp_path/'consumer_failure.json').read_text())
            assert failure['site']==10 and failure['reason']==(6 if mode=='physics-active-vehicle' else 2) and failure['native_started']==0
        (tmp_path/'consumer_failure.json').unlink()
    for mode in ('substep-initial','substep-repeat','substep-initial-null','substep-repeat-null',
                 'substep-repeat-final','substep-initial-unrelated','substep-repeat-unrelated',
                 'substep-initial-empty','substep-repeat-empty','substep-initial-invoke','substep-repeat-invoke'):
        substep=subprocess.run([str(executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
        assert substep.returncode==0,f'{mode} child exit={substep.returncode}\n'+substep.stdout+substep.stderr
        if mode in ('substep-initial','substep-repeat','substep-initial-invoke','substep-repeat-invoke'):
            assert 'substep callback rejected before native wrapper' in substep.stdout
            failure=json.loads((tmp_path/'consumer_failure.json').read_text())
            assert failure['site']==11 and failure['reason']==(12 if mode.endswith('-invoke') else 3) and failure['native_started']==0
        else:
            assert (tmp_path/'consumer_failure.json').read_bytes()==b''
        (tmp_path/'consumer_failure.json').unlink()
    # Also execute the real Windows fail-fast service in an owned child. The
    # intercepted variant above checks retained task bytes/counters; this one
    # establishes process termination, not native/GPU cleanup or B recovery.
    terminal_executable = component_dispatch_executable(fastfail=True)
    terminal=subprocess.run([str(terminal_executable)],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert terminal.returncode & 0xffffffff == 0xc0000409,terminal.stdout+terminal.stderr
    assert 'changed consumer queued; unchanged native entry count=1' in terminal.stdout
    assert 'G1 RED' not in terminal.stdout
    # The process terminates without C++ teardown: the rejection must already
    # be durable, not merely present in a buffered logger or omitted dump heap.
    assert (tmp_path/'consumer_failure.json').is_file(), 'G1 RED: terminal dispatch lost its first-failure diagnostic'
    failure=json.loads((tmp_path/'consumer_failure.json').read_text())
    assert failure['site']==2 and failure['reason']==4 and failure['native_started']==0
    assert failure['task'] and failure['function'] and failure['owner'] and failure['pid']>0
    for mode in ('valid','admission','concurrent','physics','substep-initial','substep-repeat'):
        (tmp_path/'consumer_failure.json').unlink()
        process=subprocess.Popen([str(terminal_executable),mode],cwd=tmp_path,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        stdout,stderr=process.communicate(timeout=10)
        if mode=='valid':
            assert process.returncode==0,stdout+stderr
            assert (tmp_path/'consumer_failure.json').read_bytes()==b''
        else:
            assert process.returncode & 0xffffffff == 0xc0000409,stdout+stderr
            failure=json.loads((tmp_path/'consumer_failure.json').read_text())
            assert failure['site']==(11 if mode.startswith('substep-') else 10 if mode=='physics' else 1) and failure['native_started']==0 and failure['pid']==process.pid
            if mode=='concurrent':assert 100<=failure['reason']<108

