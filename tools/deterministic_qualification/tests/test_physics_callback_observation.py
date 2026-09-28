"""Actual permanent-owner installation and entry ABI; no native lifetime emulation."""
import json
import subprocess
import pytest
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.fixture(scope='module')
def physics_observer_executable(tmp_path_factory):
    import replay_test as module
    from deterministic_iteration import VCVARS
    root=module.ROOT/'HorseMod/horselib/deterministic'
    folder=tmp_path_factory.mktemp('physics-callback-native')
    source=(root/'NativeReplayTraceTaskGuard.hpp').read_text()
    for name,start,end in (
        ('install','    static bool InstallStartup(', '    bool Bind('),
        ('startup','    struct HookStorage', '    template<class T> static T Read('),
        ('methods','    bool Matches(', '    inline static std::atomic<NativeReplayTraceTaskGuard*>')):
        begin=source.index(start)
        (folder/f'physics_{name}.inl').write_text(source[begin:source.index(end,begin)])
    exports=(module.ROOT/'HorseMod/dllmain.cpp').read_text()
    begin=exports.find('    HORSE_MOD_API bool horsemod_start_physics_callback_observation(')
    block='' if begin<0 else exports[begin:exports.index('    HORSE_MOD_API bool horsemod_start_vfx_completion_observation(',begin)]
    (folder/'physics_exports.inl').write_text(block)
    host=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    begin=host.find('            const auto physics_close=',host.index('    void CompleteTrajectory()'))
    block='' if begin<0 else host[begin:host.index('            // End physics callback observation.',begin)]
    (folder/'physics_completion.inl').write_text(block)
    # Before implementation, absence of the actual export is allowed so the
    # RED reaches both originals, then fails on absent entry/return evidence.
    (folder/'physics_resolver.inl').write_text('\n'.join(
        f'if(!std::strcmp(name,"{name}"))return reinterpret_cast<T>(&{name});'
        for name in __import__('re').findall(r'HORSE_MOD_API bool (horsemod_\w+)\(',
            (folder/'physics_exports.inl').read_text())))
    (folder/'polyhook2/Detour').mkdir(parents=True)
    (folder/'polyhook2/Detour/x64Detour.hpp').write_text('''#pragma once
namespace PLH {class x64Detour {public:
x64Detour(std::uint64_t target,std::uint64_t entry,std::uint64_t* original)
{*original=ProbeInstall(target,entry);}bool hook(){return true;}};}
''')
    fixture=module.ROOT/'tools/replay_physics_callback_observation_selftest.cpp'
    batch=folder/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{root}" "{fixture}" /Fe:physics-observation.exe /Fo:physics-observation.obj\n')
    result=run_compile(['cmd','/d','/c',str(batch)],cwd=folder,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    return folder/'physics-observation.exe'


def test_physics_writer_signatures_match_native_evidence():
    import re
    from pathlib import Path
    source=(Path(__file__).resolve().parents[3]/'HorseMod/horselib/deterministic/NativeReplayTraceTaskGuard.Signatures.inl').read_text()
    # Independent native bytes, not copied from the signature table into a fixture.
    for index,expected in ((27,'48895c241848897424205557415441564157488bec4881ec80000000488b057d1afb014833c4488945f04533e44d8bf1'),
                           (28,'488954241055574883ec2833ff488be93979500f8ed600000048895c2450488974242033f66666660f1f840000000000')):
        match=re.search(r'signature_'+str(index)+r'\[\]\{([^}]+)\}',source)
        assert match
        assert bytes(int(n,0) for n in match[1].split(','))==bytes.fromhex(expected)


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('writer-normal','writer-nested','writer-filter','writer-pending','writer-retired','writer-overflow','writer-depth','writer-readfail','writer-scenes','writer-step','writer-worker','writer-held','signature-register','signature-remove'))
def test_physics_writer_boundary(tmp_path,physics_observer_executable,mode):
    child=subprocess.run([str(physics_observer_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=60 if mode=='writer-overflow' else 45)
    assert child.returncode==0,child.stdout+child.stderr
    if mode.startswith('signature'):
        assert not (tmp_path/'physics_callback_observation.jsonl').exists()
        return
    assert 'writer forwarding PASS' in child.stdout
    path=tmp_path/'physics_callback_observation.jsonl'
    rows=[json.loads(line) for line in (path if path.exists() else path.with_name(path.name+'.tmp')).read_bytes().splitlines()]
    writers=[r for r in rows if r.get('entry_rva') in (0x21377c0,0x1f605a0)]
    assert writers, 'Actual permanent hook boundary forwards the writer but makes it invisible'
    assert rows[0]['writer_filter']=='observed_scene_collections_32'
    assert rows[0]['pre_discovery_writers_covered'] is False
    assert all(r['collection']-r['scene'] in ((0x10,0x80) if mode=='writer-step' else (0x10,)) for r in writers)
    assert all(r['scene_type']==2**32-1 for r in writers)
    assert all(r['caller'] and r['thread'] and len(r['ancestry'])==8 and any(r['ancestry']) for r in writers)
    pairs={}
    for row in writers:
        if row['boundary']=='entry':pairs[row['pair']]=row
        else:
            entry=pairs.pop(row['pair'])
            assert all(row[k]==entry[k] for k in ('collection','scene','receiver','method','handle_argument','caller','thread','ancestry'))
            if row['entry_rva']==0x21377c0:
                assert row['returned_pointer']==row['handle_argument']
                assert row['returned_handle']==(0 if mode=='writer-readfail' else 0xabcdef0123456789)
                assert row['handle_read']==(0 if mode=='writer-readfail' else 1)
    assert not pairs
    if mode=='writer-nested':assert any(r['parent_pair'] for r in writers)
    if mode=='writer-filter':assert len(writers)==4
    phase=next(r for r in rows if r['kind']=='phase')
    assert phase['complete'] is (mode not in ('writer-pending','writer-overflow','writer-depth','writer-readfail','writer-scenes'))
    if mode in ('writer-overflow','writer-depth','writer-readfail','writer-scenes'):assert phase['dropped']>0
    if mode=='writer-pending':assert phase['pending_at_close']==1 and not path.exists()
    if mode=='writer-worker':assert len({r['thread'] for r in writers})==2
    if mode=='writer-step':assert any(r['collection']-r['scene']==0x80 for r in writers)
    expected_calls={'writer-nested':4,'writer-filter':20003,'writer-retired':1,'writer-overflow':8201,'writer-depth':141}.get(mode,3)
    assert f'writer forwarding PASS calls={expected_calls}\n' in child.stdout,child.stdout
    from deterministic_qualification.replay_control import retain_physics_callback_observation
    report={}
    retain_physics_callback_observation(report,path,'physics-observation-fixture',tmp_path/'objects')
    assert 'physics_callback_observation_collection_error' not in report,report
    assert report['physics_callback_observation_phase_complete'] is phase['complete'],report



@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('normal','nested','worker','pending','overflow','crash','writefail','flush-close','signature','signature-dispatch','existing','retired','held-observer','routes'))
def test_physics_callbacks_forward_and_observe(tmp_path,physics_observer_executable,mode):
    child=subprocess.run([str(physics_observer_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=60 if mode=='overflow' else 30)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==(73 if mode=='crash' else 0),detail
    assert 'native forwarding PASS' in child.stdout,detail
    if mode.startswith('signature'):
        assert not (tmp_path/'physics_callback_observation.jsonl').exists()
        return
    if mode=='existing':
        assert (tmp_path/'physics_callback_observation.jsonl').read_bytes()==b'prior evidence'
        return
    path=tmp_path/'physics_callback_observation.jsonl'
    staging=path.with_name(path.name+'.tmp')
    assert path.is_file() or staging.is_file(), 'Both native originals forwarded, but production installed no physics callback observation'
    rows=[json.loads(line) for line in (path if path.is_file() else staging).read_bytes().splitlines()]
    assert staging.is_file() is (mode in ('pending','crash','writefail','flush-close'))
    header=rows[0]
    assert header['observation_only'] is True and header['lifetime_proven'] is False
    assert header['opaque_scene_identity'] is True and header['max_events']==16384
    assert header['owned_bytes']==2*1024*1024
    events=[r for r in rows if r['kind']=='event']
    assert events and {e['entry_rva'] for e in events}=={0x29b4530,0x20113e0}
    assert all(e['scene_type']==2 for e in events)
    assert all(e['delta_bits']==0x3c888889 for e in events if e['entry_rva']==0x20113e0)
    if mode not in ('crash','writefail','flush-close'):
        phase=next(r for r in rows if r['kind']=='phase')
        assert phase['end_tick']==483
        assert phase['complete'] is (mode not in ('pending','overflow'))
        assert phase['pending_at_close']==(1 if mode=='pending' else 0)
        assert 'pending=0' in child.stdout
        assert phase['records']==(16384 if mode=='overflow' else 12 if mode=='routes' else 3 if mode=='pending' else 6 if mode in ('nested','worker') else 4)
    if mode=='routes':
        assert {e['route'] for e in events}=={'scene_startup','scene_shutdown','physics_pre_step','physics_step',
                                            'other_lifecycle_collection','other_physics_collection'}
    if mode=='overflow':assert phase['dropped']==3 and len(events)==16384
    if mode=='nested':assert [e['depth'] for e in events]==[1,1,1,2,2,1]
    if mode=='worker':
        assert len({e['thread'] for e in events})==2
        assert any(e['other_thread_same_scene']==1 for e in events)
    if mode=='crash':
        assert len(events)==3 and events[-1]['boundary']=='entry'
        assert not any(r['kind']=='phase' for r in rows)
    if mode in ('writefail','flush-close'):assert 'persistence_failed=1' in child.stdout
    from deterministic_qualification.replay_control import retain_physics_callback_observation
    report={}
    retain_physics_callback_observation(report,path,'physics-observation-fixture',tmp_path/'objects')
    assert report['physics_callback_observation_phase_complete'] is (mode not in ('pending','overflow','crash','writefail','flush-close')),report


@pytest.mark.native_contract
def test_physics_reader_rejects_forged_completeness(tmp_path,physics_observer_executable):
    from copy import deepcopy
    from deterministic_qualification.replay_control import retain_physics_callback_observation
    child=subprocess.run([str(physics_observer_executable),'nested'],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    path=tmp_path/'physics_callback_observation.jsonl'
    rows=[json.loads(line) for line in path.read_bytes().splitlines()]
    for corruption in ('identity','pair','route','parent','overlap','phase','suffix','truncated','lifetime'):
        value=deepcopy(rows)
        if corruption=='identity':value[0]['run_id']='wrong-run'
        elif corruption=='pair':value[2]['pair']+=1
        elif corruption=='route':value[1]['route']='scene_startup'
        elif corruption=='parent':value[4]['parent_pair']=0
        elif corruption=='overlap':value[4]['other_thread_same_scene']=1
        elif corruption=='phase':value[-1]['pending_at_close']=1
        elif corruption=='suffix':value.append(value[1])
        elif corruption=='lifetime':value[0]['lifetime_proven']=True
        raw=('\n'.join(json.dumps(r) for r in value)+'\n').encode()
        if corruption=='truncated':raw=raw[:-2]
        path.write_bytes(raw);report={}
        retain_physics_callback_observation(report,path,'physics-observation-fixture',tmp_path/'objects')
        assert report['physics_callback_observation_phase_complete'] is False,corruption
        assert report['physics_callback_observation_requires_live_status'] is True,corruption
        assert 'physics_callback_observation_collection_error' in report,corruption


@pytest.mark.native_contract
def test_physics_selected_phase_capacity(tmp_path,physics_observer_executable):
    from deterministic_qualification.replay_control import retain_physics_callback_observation
    child=subprocess.run([str(physics_observer_executable),'selected-phase'],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'lifecycle=2 dispatch=4999' in child.stdout,child.stdout
    report={}
    retain_physics_callback_observation(report,tmp_path/'physics_callback_observation.jsonl',
        'physics-observation-fixture',tmp_path/'objects')
    assert report['physics_callback_observation_phase_complete'],child.stdout+str(report)
    observed=report['physics_callback_observation']
    assert observed['records']==10000
    assert observed['phase']['dropped']==observed['pending_pairs']==0
    assert observed['routes']=={'scene_shutdown':2,'physics_pre_step':4998,'physics_step':5000}


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('normal','writer-depth','writer-normal','writer-scenes'))
@pytest.mark.parametrize('predecessor',(None,b'prior report evidence'))
def test_physics_jsonl_recovery_owns_native_publication(tmp_path,monkeypatch,physics_observer_executable,mode,predecessor):
    from deterministic_qualification import process_control
    from deterministic_qualification.run_resources import RunResources, recover_resources
    monkeypatch.setattr(process_control,'list_game_processes',lambda: ())
    target=tmp_path/'physics_callback_observation.jsonl'
    if predecessor is not None:target.write_bytes(predecessor)
    journal=tmp_path/'resources.json'
    owner=RunResources(journal);owner.begin('physics-observation-fixture')
    owner.park_report(target,allow_empty=True)
    owner.prepare_temporary(target.with_name(target.name+'.tmp'))
    # Journal ownership needs a real overflow marker, not another 16K flush
    # campaign. Pending-slot/scene-registry exhaustion exercises the same actual
    # production overflow emission; capacity is tested separately above.
    child=subprocess.run([str(physics_observer_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    assert child.returncode==0,child.stdout+child.stderr
    assert target.is_file()
    recover_resources(journal)  # Actual restoration boundary, including its deployment lock.
    assert RunResources(journal).document['state']=='clean'
    assert (target.read_bytes() if target.exists() else None)==predecessor
    recover_resources(journal)  # Completed cleanup remains idempotent.


@pytest.mark.native_contract
@pytest.mark.parametrize('corruption',('foreign','child_run','missing_header','duplicate_header',
    'duplicate_key','malformed','unfinished','oversize_line','oversize_file','unknown_row',
    'extra_field','event_type','phase_suffix','other_filename','not_generated'))
def test_physics_jsonl_recovery_rejects_unowned_bytes(tmp_path,monkeypatch,physics_observer_executable,corruption):
    from deterministic_qualification import process_control
    from deterministic_qualification.run_resources import RunResources, recover_resources
    monkeypatch.setattr(process_control,'list_game_processes',lambda: ())
    target=tmp_path/'physics_callback_observation.jsonl';journal=tmp_path/'resources.json'
    if corruption=='other_filename':target=tmp_path/'unrelated.jsonl'
    owner=RunResources(journal);owner.begin('physics-observation-fixture')
    predecessor=b'prior report evidence';target.write_bytes(predecessor)
    if corruption=='not_generated':
        owner.prepare_file(target,[None]);target.unlink()
    else:owner.park_report(target,allow_empty=True)
    child=subprocess.run([str(physics_observer_executable),'normal'],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    assert child.returncode==0,child.stdout+child.stderr
    raw=(tmp_path/'physics_callback_observation.jsonl').read_bytes()
    rows=[json.loads(line) for line in raw.splitlines()]
    if corruption=='foreign':rows[0]['run_id']='foreign'
    elif corruption=='child_run':rows[0]['run_id']+='-unowned-child'
    elif corruption=='missing_header':rows.pop(0)
    elif corruption=='duplicate_header':rows.insert(1,rows[0])
    elif corruption=='unknown_row':rows[1]['kind']='unowned'
    elif corruption=='extra_field':rows[1]['unowned']='arbitrary content'
    elif corruption=='event_type':rows[1]['pair']='1'
    elif corruption=='phase_suffix':rows.append(rows[1])
    raw=('\n'.join(json.dumps(row,separators=(',',':')) for row in rows)+'\n').encode()
    if corruption=='duplicate_key':raw=raw.replace(b'"kind":"header"',b'"kind":"foreign","kind":"header"',1)
    elif corruption=='malformed':raw+=b'{bad json}\n'
    elif corruption=='unfinished':raw=raw[:-1]
    elif corruption=='oversize_line':raw=raw.replace(b'\n',b' '*1024+b'\n',1)
    elif corruption=='oversize_file':raw+=b' '*(17*1024*1024)
    target.write_bytes(raw)
    with pytest.raises(RuntimeError,match='outside this run'):recover_resources(journal)
    assert target.read_bytes()==raw
    assert RunResources(journal).document['state']=='cleanup_pending'
    assert __import__('pathlib').Path(owner.document['files'][0]['backup']).read_bytes()==predecessor


@pytest.mark.parametrize('raw,accepted',((b'{"run_id":"selected"}',True),
    (b'{"run_id":"selected-child"}',True),(b'{"run_id":"foreign"}',False),
    (b'{"run_id":"selected"}\n{"kind":"event"}\n',False)))
def test_json_report_recovery_contract_is_unchanged(tmp_path,monkeypatch,raw,accepted):
    from deterministic_qualification import process_control
    from deterministic_qualification.run_resources import RunResources, recover_resources
    monkeypatch.setattr(process_control,'list_game_processes',lambda: ())
    target=tmp_path/'existing_report.json';journal=tmp_path/'resources.json'
    owner=RunResources(journal);owner.begin('selected');owner.park_report(target)
    target.write_bytes(raw)
    if accepted:
        recover_resources(journal);assert not target.exists()
    else:
        with pytest.raises(RuntimeError,match='outside this run'):recover_resources(journal)
        assert target.read_bytes()==raw
