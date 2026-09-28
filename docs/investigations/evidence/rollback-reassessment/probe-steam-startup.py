from pathlib import Path
import subprocess
import time
import uuid
import psutil
from tools.deterministic_qualification.run_resources import RunResources

root = Path('E:/myMods/docs/investigations/evidence/rollback-reassessment')
game = Path('E:/SteamLibrary/steamapps/common/SoulcaliburVI/SoulcaliburVI/Binaries/Win64/SoulcaliburVI.exe')
resources = RunResources(root / 'steam-startup-resources.json')
resources.recover()
run_id = 'startup-' + uuid.uuid4().hex
resources.begin(run_id)
marker = '-HorseQualificationRun=' + run_id
resources.prepare_process_launch(game, marker)
try:
    subprocess.run(['C:/Program Files (x86)/Steam/steam.exe', '-applaunch', '544750', marker], timeout=10)
    deadline = time.monotonic() + 30
    observed = None
    while time.monotonic() < deadline:
        resources.recover_launched_processes()
        live = resources.live_processes()
        if live:
            if observed is None:
                observed = psutil.Process(live[0].pid)
                print('owned_pid', observed.pid, 'command', observed.cmdline(), flush=True)
            try:
                code = observed.wait(timeout=0)
                print('exit_code', code, flush=True)
                break
            except psutil.TimeoutExpired:
                pass
        elif observed is not None:
            print('game_exited', flush=True)
            break
        time.sleep(0.5)
    print('alive_at_deadline', len(resources.live_processes()), flush=True)
finally:
    resources.recover()
    print('cleanup', resources.document['state'], flush=True)
