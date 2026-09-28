import pytest
import subprocess
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.native_contract
def test_observer_accepts_two_cycle_scheduled_diagnostic_only(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source = (module.ROOT / 'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text(encoding='utf-8')
    start = source.index('        if(fields.contains("rolling_cycles")) {')
    end = source.index('        if(fields.contains("replay_budget_gib"))', start)
    body = source[start:end]
    fixture = tmp_path / 'protocol.cpp'
    fixture.write_text('''#include <cassert>
#include <string>
#include <unordered_map>
struct Output {unsigned rolling_cycles{};bool executor_control=true,host_seek=true,ground_motion_perturb=false;};
bool Parse(bool scheduled_protocol,std::unordered_map<std::string,std::string> fields,Output& output){
''' + body + '''return true;}
int main(){
 std::unordered_map<std::string,std::string> fields{{"rolling_cycles","2"},{"historical_anchor_tick","210"},{"historical_advanced_tick","217"},{"host_seek_target","217"}};
 Output output;assert(Parse(true,fields,output) && output.rolling_cycles==2);assert(!Parse(false,fields,output));
 fields["rolling_cycles"]="7";assert(Parse(true,fields,output) && output.rolling_cycles==7);assert(!Parse(false,fields,output));
 fields["rolling_cycles"]="600";assert(Parse(false,fields,output) && output.rolling_cycles==600);
 fields["rolling_cycles"]="2";fields["historical_cancel"]="true";assert(!Parse(true,fields,output));
 fields.erase("historical_cancel");fields["rolling_cycles"]="407";fields["ground_motion_perturb"]="true";
 Output motion;assert(Parse(true,fields,motion) && motion.ground_motion_perturb);
 fields["ground_motion_perturb"]="false";assert(!Parse(true,fields,motion));
 fields["ground_motion_perturb"]="true";motion.executor_control=false;assert(!Parse(true,fields,motion));
 motion.executor_control=true;fields["rolling_cycles"]="30";assert(!Parse(true,fields,motion));
}
''', encoding='utf-8')
    batch = tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{fixture}" /Fe:protocol.exe /Fo:fixture.obj\n', encoding='utf-8')
    built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / 'protocol.exe')], cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr


@pytest.mark.native_contract
def test_corrected_lighting_capture_excludes_only_unpublished_quarantine(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    directory = module.ROOT / 'HorseMod/horselib/deterministic'
    header = (directory / 'Sc6ReplayParticleCopy.hpp').read_text(encoding='utf-8')
    start = header.index('    struct LightingMap {')
    end = header.index('    bool lighting_dirty_', start)
    (tmp_path / 'lighting_capture_types.inl').write_text(header[start:end], encoding='utf-8')
    source = (directory / 'Sc6ReplayParticleCopy.Lighting.inl').read_text(encoding='utf-8')
    start = source.index('bool Sc6ReplayParticleCopy::CaptureLightingUnchecked(')
    end = source.index('bool Sc6ReplayParticleCopy::LightingBindings(', start)
    (tmp_path / 'lighting_capture_body.inl').write_text(source[start:end].replace('Sc6ReplayParticleCopy::', 'Host::'), encoding='utf-8')
    fixture = module.ROOT / 'tools/replay_corrected_lighting_capture_selftest.cpp'
    batch = tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{module.ROOT / "HorseMod"}" "{fixture}" /Fe:lighting.exe /Fo:fixture.obj\n', encoding='utf-8')
    built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / 'lighting.exe')], cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr


@pytest.mark.native_contract
def test_corrected_checkpoint_passes_existing_transaction_quarantine(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source = (module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.Checkpoint.inl').read_text(encoding='utf-8')
    start = source.index('        const auto available = participant_budget();', source.index('std::array<void*, 4096> physics_owners'))
    end = source.index('L"vfx_requests");', start) + len('L"vfx_requests");')
    (tmp_path / 'checkpoint_vfx_route.inl').write_text(source[start:end], encoding='utf-8')
    fixture = module.ROOT / 'tools/replay_checkpoint_vfx_route_selftest.cpp'
    batch = tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:route.exe /Fo:fixture.obj\n', encoding='utf-8')
    built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / 'route.exe')], cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr


@pytest.mark.native_contract
def test_replacement_waits_for_existing_advance_and_defers_failure_until_recovery(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    source = (module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.Rolling.inl').read_text(encoding='utf-8')
    start = source.index('    auto& r=rolling_;auto& w=r.witness;', source.index('bool Sc6ReplayHost::DriveRollingReplacement()'))
    end = source.index('    const auto tick=simulation_->continuation().tick;', start)
    (tmp_path / 'rolling_transition.inl').write_text(source[start:end], encoding='utf-8')
    start = source.index('    auto& r=rolling_;auto& w=r.witness;', source.index('void Sc6ReplayHost::ObserveRollingSeek('))
    end = source.index('    if(state.phase==SeekPhase::Failed', start)
    (tmp_path / 'rolling_accounting_entry.inl').write_text(source[start:end], encoding='utf-8')
    seek = (module.ROOT / 'HorseMod/horselib/deterministic/Sc6ReplayHost.Seek.inl').read_text(encoding='utf-8')
    start = seek.index('    if(seek_retirement_pending_ && witness.phase!=SeekPhase::CompletingApplication)')
    end = seek.index('    const bool native_recovery=', start)
    (tmp_path / 'seek_accounting_entry.inl').write_text(seek[start:end], encoding='utf-8')
    fixture = module.ROOT / 'tools/replay_rolling_transition_selftest.cpp'
    batch = tmp_path / 'compile.cmd'
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. "{fixture}" /Fe:transition.exe /Fo:fixture.obj\n', encoding='utf-8')
    built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path, capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / 'transition.exe')], cwd=tmp_path, capture_output=True, text=True, timeout=10)
    assert checked.returncode == 0, checked.stdout + checked.stderr
