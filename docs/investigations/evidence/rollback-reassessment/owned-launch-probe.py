"""One bounded, non-certifying check of deployment/launch ownership."""
import json
from pathlib import Path
import sys
import time
import uuid

ROOT = Path(r"E:\myMods")
sys.path.insert(0, str(ROOT))
import psutil
from tools.deterministic_qualification.artifacts import sha256_file
from tools.deterministic_qualification.configuration import require_disarmed
from tools.deterministic_qualification.observer_pair import SAFE_CONFIG, OBSERVER_CONFIG
from tools.deterministic_qualification.paired_online import _deploy_owned_pair, _launch_owned_pair, _require_no_stale_replay_requests
from tools.deterministic_qualification.process_control import list_game_processes
from tools.deterministic_qualification.runner import _observer_paths
from tools.deterministic_qualification.run_resources import RunResources, acquire_deployment_lock, deployment_journal_path, bytes_hash
from tools.deterministic_qualification.run_journal import atomic_json
from tools.deterministic_qualification.sandboxie_pair import SandboxiePairSpec, classify_game_processes, list_sandbox_pids

paths = _observer_paths(Path(r"C:\Sandbox\prest\sc67"))
spec = SandboxiePairSpec(box_name="sc67")
dll = ROOT / "build_cmake_LessEqual421__Shipping__Win64/HorseMod/HorseMod.dll"
bridge = dll.with_name("ReplayQualificationMod.dll")
report_path = Path(__file__).with_suffix(".json")
report = {"kind": "owned_launch_cleanup_probe", "certifying": False, "result": "fail",
          "runtime_sha256": sha256_file(dll), "observations": []}
journal = deployment_journal_path(paths.host.horsemod_dll)
with acquire_deployment_lock(journal):
    resources = RunResources(journal)
    resources.recover()
    _require_no_stale_replay_requests(paths)
    if list_game_processes():
        raise RuntimeError("preexisting game prevents probe")
    resources.begin("launch-probe-" + uuid.uuid4().hex)
    try:
        for peer in (paths.host, paths.sandbox):
            if peer.config.exists():
                require_disarmed(peer.config)
            resources.prepare_file(peer.horsemod_dll, [sha256_file(dll)])
            resources.prepare_file(peer.replay_mod_root / "dlls/main.dll", [sha256_file(bridge)])
            resources.prepare_file(peer.replay_mod_root / "enabled.txt", [bytes_hash(
                "# Qualification-only bridge; presence of this file enables the mod.\n")])
            resources.prepare_file(peer.config, [bytes_hash(SAFE_CONFIG), bytes_hash(OBSERVER_CONFIG)])
            resources.prepare_requests(peer.qualification_root)
        _deploy_owned_pair(paths, dll, bridge, resources)
        _launch_owned_pair(spec, resources)
        deadline = time.monotonic() + 300
        mapped = {}
        while time.monotonic() < deadline:
            resources.recover_launched_processes()
            owned = resources.live_processes()
            for row in resources.document["processes"]:
                if row["pid"] not in {p.pid for p in owned}:
                    raise RuntimeError("owned game exited during module admission")
            if len(owned) == 2:
                pair = classify_game_processes({p.pid for p in owned}, set(list_sandbox_pids(spec)))
                for role, pid, peer in (("host", pair.host_pid, paths.host),
                                        ("sandbox", pair.sandbox_pid, paths.sandbox)):
                    if role in mapped:
                        continue
                    process = psutil.Process(pid)
                    modules = {Path(m.path).resolve() for m in process.memory_maps() if m.path}
                    if peer.horsemod_dll.resolve() in modules:
                        mapped[role] = {"pid": pid, "created": process.create_time(),
                            "module": str(peer.horsemod_dll), "sha256": sha256_file(peer.horsemod_dll)}
                        print(role + " mapped requested runtime", flush=True)
                if len(mapped) == 2:
                    report["observations"] = mapped
                    report["result"] = "pass"
                    break
            time.sleep(0.25)
        else:
            raise RuntimeError("module admission deadline exceeded")
    except BaseException as error:
        report["failure"] = str(error)
        report["result"] = "fail"
    finally:
        try:
            resources.recover()
            report["cleanup"] = {"resource_state": resources.document["state"],
                "game_processes_remaining": len(list_game_processes()),
                "deployment_restored": resources.document["state"] == "clean"}
        except BaseException as error:
            report["cleanup_failure"] = str(error)
            report["result"] = "fail"
        atomic_json(report_path, report)
print(json.dumps(report), flush=True)
raise SystemExit(0 if report["result"] == "pass" else 1)
