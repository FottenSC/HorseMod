import pytest
from pathlib import Path
import subprocess


@pytest.mark.native_contract
def test_real_compiler_cache_reuses_build_but_header_shadowing_invalidates(tmp_path, monkeypatch):
    from tools.deterministic_qualification.fixture_cache import run_compile
    from deterministic_iteration import VCVARS
    monkeypatch.setenv('HORSE_FIXTURE_CACHE',str(tmp_path/'cache'))
    working=tmp_path/'working';working.mkdir()
    first=working/'first';second=working/'second';first.mkdir();second.mkdir()
    (second/'value.hpp').write_text('#define VALUE 3\n')
    (working/'main.cpp').write_text('#include "value.hpp"\nint main(){return VALUE;}\n')
    batch=working/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /EHsc /Ifirst /Isecond main.cpp /Fe:test.exe /Fo:test.obj\n')
    def build(): return run_compile(['cmd','/d','/c',str(batch)],cwd=working,capture_output=True,text=True,timeout=60)
    a=build();assert a.returncode==0,a.stdout+a.stderr
    b=build();assert b.returncode==0,b.stdout+b.stderr
    assert a.fixture_cache['status']=='miss' and b.fixture_cache['status']=='hit'
    assert subprocess.run([str(working/'test.exe')]).returncode==3
    (first/'value.hpp').write_text('#define VALUE 4\n')
    c=build();assert c.fixture_cache['status']=='miss'
    assert subprocess.run([str(working/'test.exe')]).returncode==4
    entry=tmp_path/'cache'/c.fixture_cache['key']
    import json
    receipt=json.loads((entry/'receipt.json').read_bytes())
    (entry/receipt['outputs'][0]['name']).write_bytes(b'corrupt')
    d=build();assert d.fixture_cache['status']=='miss' and d.returncode==0
    assert 'corrupt' in d.fixture_cache['reason']
    assert build().fixture_cache['status']=='hit'
    batch.write_text(batch.read_text().replace('/EHsc','/EHsc /DNEW_FLAG=1'))
    e=build();assert e.fixture_cache['status']=='miss' and e.returncode==0
    assert 'recipe' in e.fixture_cache['reason']
    monkeypatch.setenv('CL','/Od')
    f=build();assert f.fixture_cache['status']=='miss' and f.returncode==0
    assert 'environment' in f.fixture_cache['reason']
    metadata=tmp_path/'cache'/f.fixture_cache['key']/'receipt.json'
    damaged=json.loads(metadata.read_bytes());damaged['key']['recipe']='receipt from another compilation'
    metadata.write_text(json.dumps(damaged))
    g=build();assert g.fixture_cache['status']=='miss' and g.returncode==0
    assert 'identity' in g.fixture_cache['reason']
    metadata.unlink()
    h=build();assert h.fixture_cache['status']=='miss' and h.returncode==0
    assert 'incomplete' in h.fixture_cache['reason']


@pytest.mark.native_contract
def test_red_override_never_reuses_cached_build(tmp_path,monkeypatch):
    from tools.deterministic_qualification.fixture_cache import run_compile
    monkeypatch.setenv('HORSE_VFX_OBSERVER_RED_HEADER','retained-red.hpp')
    batch=tmp_path/'compile.cmd';batch.write_text('@exit /b 0\n')
    result=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert result.returncode==0 and result.fixture_cache['status']=='bypass'


@pytest.mark.native_contract
def test_object_link_inputs_are_not_mistaken_for_preprocessor_dependencies(tmp_path,monkeypatch):
    from tools.deterministic_qualification import fixture_cache
    from deterministic_iteration import VCVARS
    batch=tmp_path/'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /EHsc main.cpp support.obj /Fe:test.exe /Fo:test.obj\n')
    calls=[]
    def execute(command,**kwargs):
        calls.append(command);return subprocess.CompletedProcess(command,0,'','')
    monkeypatch.setattr(fixture_cache.subprocess,'run',execute)
    command=['cmd','/d','/c',str(batch)]
    result=fixture_cache.run_compile(command,cwd=tmp_path,capture_output=True,text=True)
    assert calls==[command]
    assert result.fixture_cache['status']=='bypass' and 'explicit link input' in result.fixture_cache['reason']
