import pytest
import subprocess
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.native_contract
def test_production_gpu_image_equality(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS
    fixture = module.ROOT / 'tools/replay_gpu_image_equality_selftest.cpp'
    includes = module.ROOT / 'HorseMod/horselib'
    shader = includes / 'deterministic/ReplayGpuImageEquality.hlsl'
    batch = tmp_path / 'compile.cmd'
    batch.write_text(
        f'@echo off\ncall "{VCVARS}" >nul\n'
        f'fxc /nologo /T cs_5_0 /E main /O3 /Fh ReplayGpuImageEqualityShader.hpp /Vn replay_gpu_image_equality_shader "{shader}"\n'
        f'if errorlevel 1 exit /b 1\n'
        f'fxc /nologo /T cs_5_0 /E main /O3 /Fh ReplayGpuImageExpandShader.hpp /Vn replay_gpu_image_expand_shader "{includes / "deterministic/ReplayGpuImageExpand.hlsl"}"\n'
        f'if errorlevel 1 exit /b 1\n'
        f'cl /nologo /std:c++20 /EHsc /I. /I"{includes}" "{fixture}" /Fe:gpu-equality.exe /Fo:fixture.obj /link d3d11.lib\n',
        encoding='utf-8')
    built = run_compile(['cmd', '/d', '/c', str(batch)], cwd=tmp_path,
                           capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    checked = subprocess.run([str(tmp_path / 'gpu-equality.exe')], cwd=tmp_path,
                             capture_output=True, text=True, timeout=30)
    assert checked.returncode == 0, checked.stdout + checked.stderr
    assert 'exact bits, completion identity and capacity passed' in checked.stdout
