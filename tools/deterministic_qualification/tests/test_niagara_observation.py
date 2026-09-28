import pytest
"""Actual startup/forwarding boundaries; external hook and object-table services are probes."""
import subprocess
from pathlib import Path
import json
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.native_contract
def test_startup_installs_forwarding_niagara_observer(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source=(module.ROOT/'HorseMod/HorseModService.PublicApiAndLifetime.inl').read_text()
    start=source.index('            const bool startup=RC::IsInitialCppModStartupThread();')
    end=source.index('        }\n        m_replay_native_runtime',start)
    fixture=r'''
#include <cstdint>
#include <cstddef>
#define STR(x) x
static unsigned niagara_installs;
void* GetModuleHandleW(void*){return reinterpret_cast<void*>(0x140000000ull);}
namespace RC {bool IsInitialCppModStartupThread(){return true;}}
namespace Horse::Deterministic {
struct NativeReplayTraceTaskGuard {static bool InstallStartup(std::uintptr_t,bool){return true;}static std::size_t ProcessOwnedBytes(){return 1;}};
struct NativeReplayNiagaraObservation {static bool InstallStartup(std::uintptr_t,bool b){niagara_installs+=b;return b;}static std::size_t ProcessOwnedBytes(){return 2;}};
}
enum class LogLevel {Default};struct Output {template<LogLevel,class...T>static void send(T...) {}};
int main(){
''' +source[start:end]+r'''
return niagara_installs==1?0:81;
}
'''
    cpp=tmp_path/'startup.cpp';cpp.write_text(fixture)
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:startup.exe /Fo:startup.obj\n')
    result=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    run=subprocess.run([str(tmp_path/'startup.exe')],capture_output=True,text=True,timeout=10)
    assert run.returncode==0,(run.returncode,run.stdout,run.stderr)


@pytest.mark.native_contract
def test_native_niagara_forwards_abi_nested_calls_and_bounds_first_events(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    (tmp_path/'Unreal').mkdir();(tmp_path/'polyhook2/Detour').mkdir(parents=True)
    (tmp_path/'Unreal/UObject.hpp').write_text('#pragma once\n')
    (tmp_path/'Unreal/UObjectArray.hpp').write_text('''#pragma once
namespace RC::Unreal {struct Item {void* object{};int serial{};void* GetUObject(){return object;}bool IsValid(bool){return object!=nullptr;}int GetSerialNumber(){return serial;}};
struct FUObjectArray {inline static Item items[4];static Item* IndexToObject(int i){return i>=0&&i<4?&items[i]:nullptr;}};}
''')
    (tmp_path/'polyhook2/Detour/x64Detour.hpp').write_text('''#pragma once
static void CaptureHook(std::uint64_t,std::uint64_t);
namespace PLH {class x64Detour {public:x64Detour(std::uint64_t target,std::uint64_t replacement,std::uint64_t* original){CaptureHook(target,replacement);*original=StubOriginal(target);}bool hook(){return true;}};}
''')
    fixture=module.ROOT/'tools/replay_niagara_observation_selftest.cpp'
    include=module.ROOT/'HorseMod/horselib/deterministic'
    batch=tmp_path/'compile.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{include}" "{fixture}" /Fe:observation.exe /Fo:observation.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    for mode in ('registration','registration-nested','registration-retire','registration-other','registration-invalid','registration-full','registration-signature','normal','invalid','null','zero','writefail'):
        folder=tmp_path/mode;folder.mkdir()
        run=subprocess.run([str(tmp_path/'observation.exe'),mode],cwd=folder,capture_output=True,text=True,timeout=10)
        assert run.returncode==0,(mode,run.returncode,run.stdout,run.stderr)
        if mode=='registration-signature':assert not (folder/'niagara_observation.json').exists();continue
        raw=(folder/'niagara_observation.json').read_bytes();assert len(raw)<=32768
        report=json.loads(raw);assert report['run_id']=='fixture' and report['version']==2
        assert report['registration_snapshot']=='pre_original_only'
        rows=[dict(zip(report['fields'],row)) for row in report['events']]
        from deterministic_qualification.replay_control import retain_niagara_observation
        parsed={};retain_niagara_observation(parsed,folder/'niagara_observation.json','fixture',tmp_path/'objects')
        assert parsed.get('niagara_observation')==report,parsed
        if mode=='registration-full':
            assert len(rows)==24
            for helper,rva in enumerate((0x1bcc5a0,0x1bcc730,0x1d58ba0)):
                for scope in range(4):
                    pair=[r for r in rows if r['pair']==helper*4+scope]
                    assert [r['boundary'] for r in pair]==[0,1] and all(r['helper_rva']==rva for r in pair)
            continue
        if mode.startswith('registration'):
            if mode=='registration-other':assert not rows;continue
            assert len(rows)==2 and [r['boundary'] for r in rows]==[0,1]
            assert all(r['helper_rva']==0x1d58ba0 and r['scope']==0 and r['pair']==8 for r in rows)
            assert rows[0]['input']==rows[0]['component'] and rows[0]['component_index']==2
            assert rows[0]['component_valid']==(2 if mode=='registration-invalid' else 1)
            assert rows[0]['world_index']==3 and rows[0]['world_valid']==1
            assert rows[0]['caller'] and rows[0]['thread'] and rows[0]['epoch']==1234
            assert {k:v for k,v in rows[0].items() if k!='boundary'}=={k:v for k,v in rows[1].items() if k!='boundary'}
            continue
        assert len(rows)==(0 if mode in ('zero','writefail') else 16)
        for helper in (0x1bcc5a0,0x1bcc730):
            for scope in range(4):
                pair=[row for row in rows if row['helper_rva']==helper and row['scope']==scope]
                if mode in ('zero','writefail'):continue
                assert [row['boundary'] for row in pair]==[0,1]
                assert all(row['native_tick']==211 and row['epoch']==1234 and row['thread'] for row in pair)
                assert all(row['transaction_phase']==(0xffffffff,2,9,10)[scope] for row in pair)
                assert pair[0]['component']==0 and pair[0]['input_valid']==(0 if mode=='invalid' else 1)
                assert pair[1]['component_valid']==(0 if mode=='null' else 2 if mode=='invalid' else 1)
                if mode!='null':assert pair[1]['world_valid']==1 and pair[1]['component_index']==2
        assert not (folder/'other.json').exists()


def test_niagara_collection_and_journal_restore(tmp_path,monkeypatch):
    import pytest
    from deterministic_qualification.replay_control import retain_niagara_observation
    from deterministic_qualification.run_resources import RunResources
    from deterministic_qualification.replay_evidence import store_bytes
    monkeypatch.setattr('deterministic_qualification.process_control.list_game_processes',lambda:())
    target=tmp_path/'niagara_observation.json';target.write_bytes(b'prior retained evidence')
    journal=tmp_path/'resources.json';resources=RunResources(journal);resources.begin('owned');resources.park_report(target,allow_empty=True)
    good=json.dumps(dict(run_id='owned',version=1,fields=['field']*26,events=[])).encode()
    for raw in (good,b'{broken',b'x'*32769,json.dumps(dict(run_id='foreign',version=1)).encode()):
        target.write_bytes(raw);report={'failure':'original'}
        retain_niagara_observation(report,target,'owned',tmp_path/'objects')
        assert report['niagara_observation_raw']==store_bytes(tmp_path/'objects',raw)
        assert report['failure']=='original'
        assert ('niagara_observation' in report)==(raw==good)
    with pytest.raises(RuntimeError,match='outside this run'):RunResources(journal).recover()
    target.write_bytes(good);RunResources(journal).recover()
    assert target.read_bytes()==b'prior retained evidence'


def test_niagara_registration_reader_rejects_forged_producer_and_return(tmp_path):
    from copy import deepcopy
    from deterministic_qualification.replay_control import retain_niagara_observation
    fields=('helper_rva scope boundary native_tick transaction_phase epoch thread caller caller_rva '
            'host_world pair input input_index input_serial input_valid input_table component '
            'component_index component_serial component_valid component_table world world_index '
            'world_serial world_valid world_table').split()
    entry=[0]*26;entry[0]=0x1d58ba0;entry[10]=8
    returned=entry.copy();returned[2]=1
    good=dict(run_id='fixture',version=2,registration_snapshot='pre_original_only',fields=fields,events=[entry,returned])
    cases=[good]
    for change in ('version','semantics','fields','pair','helper','scope','return','duplicate','unmatched','bound'):
        bad=deepcopy(good)
        if change=='version':bad['version']=3
        elif change=='semantics':bad['registration_snapshot']='completed'
        elif change=='fields':bad['fields'][16]='returned_component'
        elif change=='pair':bad['events'][0][10]=0
        elif change=='helper':bad['events'][0][0]=0x1bee900
        elif change=='scope':bad['events'][0][1]=4
        elif change=='return':bad['events'][1][18]=1
        elif change=='duplicate':bad['events'].insert(1,entry)
        elif change=='unmatched':bad['events']=[returned]
        elif change=='bound':bad['events']=[entry]*25
        cases.append(bad)
    for i,value in enumerate(cases):
        path=tmp_path/f'{i}.json';path.write_text(json.dumps(value))
        report={};retain_niagara_observation(report,path,'fixture',tmp_path/'objects')
        assert ('niagara_observation' in report)==(i==0),report
        assert 'niagara_observation_raw' in report
