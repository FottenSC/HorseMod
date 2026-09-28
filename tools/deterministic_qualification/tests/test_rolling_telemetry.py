import pytest
from pathlib import Path
import subprocess
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.native_contract
def test_production_timer_retains_waits_aborts_and_terminal_window(tmp_path):
    from deterministic_iteration import ROOT, VCVARS
    (tmp_path/'timer.cpp').write_text(r'''
#include "HorseMod/horselib/deterministic/ReplayRollingTelemetry.hpp"
#include <cassert>
using Horse::Deterministic::ReplayRollingTelemetry;
int main(){
 ReplayRollingTelemetry t;
 assert(t.Begin(1,217,100));
 t.Mark(ReplayRollingTelemetry::Phase::Resimulation,200);
 t.Backlog(4);
 assert(!t.Complete(300,false,true)); // no checkpoint
 assert(!t.Complete(400,true,false)); // GPU/deferred work remains
 t.Mark(ReplayRollingTelemetry::Phase::Forward,500);
 assert(t.Complete(1000,true,true));
 assert(t.rows[0].elapsed_us==900 && t.rows[0].backlog==4);
 assert(t.Begin(2,218,1100));t.Abort(1400);
 assert(t.rows[1].status==ReplayRollingTelemetry::Status::Aborted && t.rows[1].elapsed_us==300);
 assert(t.Begin(3,219,1500));t.Terminal(1700);
 assert(t.rows[2].status==ReplayRollingTelemetry::Status::Terminal);
 assert(t.size==3 && !t.active);
 for(unsigned i=3;i<600;++i){assert(t.Begin(i+1,i+217,1800));t.Abort(1900);}
 assert(!t.Begin(601,900,2000));
}
''')
    batch=tmp_path/'build.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I"{ROOT}" timer.cpp /Fe:timer.exe\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    assert subprocess.run([str(tmp_path/'timer.exe')],timeout=10).returncode==0


def test_short_authored_request_is_distinct_from_rolling_schedule():
    from tools.deterministic_qualification.replay_control import authored_prefix_request
    wire='217:0:0:172:2:0:0;218:1:0:170:2:0:0;219:2:0:168:2:0:0'
    assert authored_prefix_request(wire)==('17','authored_prefix='+wire+'\n')
    import pytest
    with pytest.raises(ValueError):authored_prefix_request('217:0:0:172:2:0:1')


@pytest.mark.native_contract
def test_native_authored_protocol_rejects_other_windows_and_faults(tmp_path):
    from deterministic_iteration import ROOT, VCVARS
    source=(ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    a=source.index('    const bool authored_protocol=')
    b=source.index('    const bool mutation_protocol=',a)
    (tmp_path/'decoder.cpp').write_text('#include <map>\n#include <string>\n#include <cassert>\nusing Fields=std::map<std::string,std::string>;\nbool decode(Fields fields){\n'+source[a:b]+r'''
return true;
}
int main(){
 Fields f{{"version","17"},{"capture_mode","trajectory"},{"authored_prefix","217:0:0:172:2:0:0;218:1:0:170:2:0:0;219:2:0:168:2:0:0"}};
 assert(decode(f));
 auto bad=f;bad["capture_mode"]="executor_match";assert(!decode(bad));
 f["capture_mode"]="executor";assert(!decode(f));
 f["historical_anchor_tick"]="617";f["historical_advanced_tick"]="624";f["host_seek_target"]="624";
 f["host_seek"]="true";f["historical_exact_advance"]="true";assert(decode(f));
 for(const auto* key:{"rolling_cycles","seek_observer_failure","seek_settlement_failure","host_seek_repeat"}){
   bad=f;bad[key]="true";assert(!decode(bad));
 }
 bad=f;bad["version"]="13";assert(!decode(bad));
}
''')
    batch=tmp_path/'build.cmd';batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc decoder.cpp /Fe:decoder.exe\n')
    result=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stdout+result.stderr
    assert subprocess.run([str(tmp_path/'decoder.exe')],capture_output=True,text=True,timeout=10).returncode==0
