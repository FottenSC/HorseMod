import pytest
"""Extracted diagnostic control flow only; no native fatal-causation proof."""
import json
import subprocess
from tools.deterministic_qualification.fixture_cache import run_compile


@pytest.mark.workflow
def test_consumer_failure_collector_preserves_crash_bytes_and_run_identity(tmp_path):
    from deterministic_qualification.replay_control import retain_consumer_failure
    from deterministic_qualification.replay_evidence import store_bytes
    path=tmp_path/'consumer_failure.json';objects=tmp_path/'objects'
    report={}
    retain_consumer_failure(report,path,'owned',objects)
    assert not report
    path.write_bytes(b'')
    retain_consumer_failure(report,path,'owned',objects)
    assert report['consumer_failure_preopened_empty'] is True
    assert report['consumer_failure_raw']==store_bytes(objects,b'')
    assert 'consumer_failure' not in report and 'consumer_failure_collection_error' not in report
    report={}
    raw=b'{"run_id":"owned","version":1,"site":3,"reason":8}'
    path.write_bytes(raw)
    retain_consumer_failure(report,path,'owned',objects)
    assert report['consumer_failure']['reason']==8
    assert report['consumer_failure_raw']==store_bytes(objects,raw)
    for raw in (b'{broken',b'{"run_id":"other","version":1}',b'x'*2049):
        path.write_bytes(raw);report={'failure':'original crash'}
        retain_consumer_failure(report,path,'owned',objects)
        assert report['failure']=='original crash'
        assert report['consumer_failure_raw']==store_bytes(objects,raw)
        assert report['consumer_failure_collection_error'] and 'consumer_failure' not in report


@pytest.mark.workflow
def test_consumer_preopened_empty_cleanup_uses_durable_runner_permission(tmp_path,monkeypatch):
    import pytest
    from deterministic_qualification.run_resources import RunResources
    from deterministic_qualification import replay_control
    from pathlib import Path
    monkeypatch.setattr('deterministic_qualification.process_control.list_game_processes',lambda: ())
    source=Path(replay_control.__file__).read_text()
    registration=next(line.strip() for line in source.splitlines() if 'resources.park_report(consumer_failure' in line)
    for prior in (None,b'{"run_id":"prior","evidence":42}'):
        folder=tmp_path/('absent' if prior is None else 'prior');folder.mkdir()
        path=folder/'consumer_failure.json';journal=folder/'resources.json'
        if prior is not None:path.write_bytes(prior)
        resources=RunResources(journal);resources.begin('owned')
        exec(registration,{'resources':resources,'consumer_failure':path})
        assert not path.exists()
        path.write_bytes(b'')
        RunResources(journal).recover()
        assert RunResources(journal).document['state']=='clean'
        assert (path.read_bytes() if path.exists() else None)==prior
    for generic in (False,True):
        folder=tmp_path/str(generic);folder.mkdir()
        path=folder/'report.json';journal=folder/'resources.json'
        resources=RunResources(journal);resources.begin('owned')
        if generic:resources.park_report(path)
        else:exec(registration,{'resources':resources,'consumer_failure':path})
        raw=b'' if generic else b'{"run_id":"foreign"}'
        path.write_bytes(raw)
        with pytest.raises(RuntimeError,match='outside this run'):RunResources(journal).recover()
        assert path.read_bytes()==raw


@pytest.mark.workflow
def test_startup_failure_collector_retains_raw_before_parsing(tmp_path):
    from deterministic_qualification.replay_control import retain_startup_failure
    from deterministic_qualification.replay_evidence import store_bytes
    path = tmp_path / 'failure.json'
    objects = tmp_path / 'objects'
    report = {}
    retain_startup_failure(report, path, 'owned', objects)
    assert not report
    raw = b'{"run_id":"owned","site":"140E0E590","error":"native cause"}'
    path.write_bytes(raw)
    retain_startup_failure(report, path, 'owned', objects)
    assert report['native_startup_failure']['error'] == 'native cause'
    assert report['native_startup_failure']['raw'] == store_bytes(objects, raw)
    for raw in (b'{broken', b'{"run_id":"other"}'):
        path.write_bytes(raw)
        report = {'failure': 'original process failure'}
        retain_startup_failure(report, path, 'owned', objects)
        assert report['failure'] == 'original process failure'
        assert report['native_startup_failure_raw'] == store_bytes(objects, raw)
        assert report['native_startup_failure_collection_error']
        assert 'native_startup_failure' not in report
    path.write_bytes(b'x' * 16385)
    report = {}
    retain_startup_failure(report, path, 'owned', objects)
    assert report['native_startup_failure_collection_error']
    assert 'native_startup_failure' not in report


@pytest.mark.native_contract
def test_startup_failure_records_bounded_first_error_and_always_forwards(tmp_path):
    import replay_test as module
    from deterministic_iteration import VCVARS

    header = (module.ROOT / "tools/replay_qualification_mod/ReplayStartupFailure.hpp").read_text(encoding="utf-8")
    # Retain the actual ErrorText, Entry, Record and their state. Installation,
    # module pinning and Polyhook are deliberately outside this unit boundary.
    start = header.index("    static std::size_t ErrorText(")
    end = header.index("    std::unique_ptr<PLH::x64Detour> hook_;", start)
    extracted = header[start:end]
    fixture = r'''
#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <iterator>
class ReplayStartupFailure final {
public:
''' + extracted + r'''
};
static unsigned calls;
static unsigned exit_calls;
static bool exit_argument;
static void OriginalExit(bool force) { ++exit_calls;exit_argument=force; }
static EXCEPTION_POINTERS* expected_exception;
static std::uint32_t OriginalException(EXCEPTION_POINTERS* value) {
    ++exit_calls;
    return value==expected_exception?0xdecafbad:0;
}
static bool forwarded=true;
static const wchar_t* expected_text;
static void* expected_device=reinterpret_cast<void*>(0x12345678);
static constexpr std::uintptr_t expected_category=0x123456789abcdef0ull;
static void Original(void* device,const wchar_t* text,unsigned char verbosity,std::uintptr_t category) {
    ++calls;
    forwarded=forwarded && device==expected_device && text==expected_text &&
        verbosity==0xd3 && category==expected_category;
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    const std::wstring mode=argv[1];
    ReplayStartupFailure state;
    ReplayStartupFailure::owner_=&state;
    state.original_=reinterpret_cast<std::uint64_t>(&Original);
    state.original_exit_=reinterpret_cast<std::uint64_t>(&OriginalExit);
    state.original_exception_=reinterpret_cast<std::uint64_t>(&OriginalException);
    state.base_=0x140000000ull;
    std::strcpy(state.run_.data(),"diagnostic-control-flow");
    const auto root=std::filesystem::current_path()/(mode==L"failure"?L"missing-directory":L".");
    const auto path=(root/L"consumer_startup_failure.json").wstring();
    const auto temporary=(root/L"consumer_startup_failure.tmp").wstring();
    std::memcpy(state.path_.data(),path.c_str(),(path.size()+1)*sizeof(wchar_t));
    std::memcpy(state.temporary_.data(),temporary.c_str(),(temporary.size()+1)*sizeof(wchar_t));
    if(mode==L"exception") {
        EXCEPTION_RECORD record{};CONTEXT context{};
        record.ExceptionCode=0xc0000005;record.ExceptionAddress=reinterpret_cast<void*>(0x140001234ull);
        record.NumberParameters=2;record.ExceptionInformation[0]=0;record.ExceptionInformation[1]=0x18;
        context.Rip=0x140001234ull;context.Rsp=0x12340000;context.Rcx=0x88776655;
        EXCEPTION_POINTERS pointers{&record,&context};expected_exception=&pointers;
        if(state.ExceptionEntry(&pointers)!=0xdecafbad || exit_calls!=1 || state.recorded_!=1)return 13;
        if(state.ExceptionEntry(&pointers)!=0xdecafbad || exit_calls!=2)return 14;
        return 0;
    }
    if(mode==L"exit") {
        state.ExitEntry(false);
        if(exit_calls!=1 || exit_argument || state.recorded_)return 10;
        state.ExitEntry(true);
        if(exit_calls!=2 || !exit_argument || state.recorded_!=1)return 11;
        state.ExitEntry(true);
        return exit_calls==3 && exit_argument?0:12;
    }
    wchar_t scratch[4]{};
    if(state.ErrorText(nullptr,scratch,4)!=0)return 2;
    if(state.ErrorText(reinterpret_cast<const wchar_t*>(1),scratch,4)!=0)return 3;
    if(state.ErrorText(L"abcdef",scratch,4)!=3 || scratch[0]!=L'a' || scratch[2]!=L'c')return 4;
    const std::wstring first=mode==L"long"?std::wstring(900,L'x'):L"quote\" slash\\ newline\n tab\t snowman\u2603";
    expected_text=first.c_str();
    state.Entry(expected_device,expected_text,0xd3,expected_category);
    if(calls!=1 || !forwarded || state.recorded_!=1)return 5;
    // A second fatal must still forward the exact original arguments while
    // preserving first-record ownership, including after CreateFile failure.
    const std::wstring second=L"second error must not replace first";
    expected_text=second.c_str();
    state.Entry(expected_device,expected_text,0xd3,expected_category);
    if(calls!=2 || !forwarded)return 6;
    return 0;
}
'''
    cpp = tmp_path / "startup_failure.cpp"
    cpp.write_text(fixture, encoding="utf-8")
    batch = tmp_path / "compile.cmd"
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc "{cpp}" /Fe:startup-failure.exe /Fo:startup-failure.obj\n', encoding="utf-8")
    built = run_compile(["cmd", "/d", "/c", str(batch)], cwd=tmp_path,
                           capture_output=True, text=True, timeout=60)
    assert built.returncode == 0, built.stdout + built.stderr
    for mode in ("first", "long", "failure", "exit", "exception"):
        case = tmp_path / mode
        case.mkdir()
        checked = subprocess.run([str(tmp_path / "startup-failure.exe"), mode], cwd=case,
                                 capture_output=True, text=True, timeout=10)
        assert checked.returncode == 0, f"{mode}: exit={checked.returncode}\n" + checked.stdout + checked.stderr
        receipt = case / "consumer_startup_failure.json"
        if mode == "failure":
            assert not receipt.exists()
            assert not (case / "missing-directory").exists()
            continue
        raw = receipt.read_bytes()
        assert len(raw) < 8192
        report = json.loads(raw)
        assert report["run_id"] == "diagnostic-control-flow"
        assert report["site"] == ("140E0D330" if mode == "exception" else "140E0E590" if mode == "exit" else "140E0ED30")
        assert report["game_base"] == "0x0000000140000000"
        assert report["error"] == ("" if mode in ("exit", "exception") else "x" * 767 if mode == "long" else 'quote" slash\\ newline\n tab\t snowman\u2603')
        assert report["error_source"] == ("native_exception_context" if mode == "exception" else "native_history_may_be_stale" if mode == "exit" else "incoming_serialize")
        if mode == 'exception':
            assert report['exception']['valid'] is True
            assert report['exception']['code'] == '0x00000000c0000005'
            assert report['exception']['rip'] == '0x0000000140001234'
            assert report['exception']['rcx'] == '0x0000000088776655'
            assert report['exception']['parameters'] == ['0x0000000000000000','0x0000000000000018']
        assert len(report["error"].encode("utf-16-le")) <= 767 * 2
        assert len(report["stack"]) <= 32
        assert not (case / "consumer_startup_failure.tmp").exists()
        if "error_at_capacity" in report:
            assert report["error_at_capacity"] is (mode == "long")
