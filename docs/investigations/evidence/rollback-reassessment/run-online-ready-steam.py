from pathlib import Path
import hashlib
import json
import subprocess
import sys
from tools.deterministic_qualification.run_journal import StageJournal
from tools.deterministic_qualification.run_resources import RunResources
from tools.deterministic_qualification.process_control import list_game_processes
from tools.deterministic_qualification.artifacts import online_capture_harness_sha256
root = Path('E:/myMods')
evidence = root / 'docs/investigations/evidence/rollback-reassessment'
old = json.loads((evidence / 'online-forward-boundary/stages.json').read_text())['stages']['round2-forward-boundary']
output = evidence / 'online-ready-steam'
output.mkdir(exist_ok=True)
command = old['specification']['command'][:]
command[command.index('--output-dir') + 1] = str(output)
report = output / 'report.json'
command[command.index('--report') + 1] = str(report)
dependencies = {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in old['dependencies']}
dependencies['capture_harness'] = online_capture_harness_sha256(root)
resources = RunResources(output / 'resources.json')
def cleanup():
    resources.__init__(output / 'resources.json')
    resources.recover()
    if list_game_processes():
        raise RuntimeError('game remains after stage cleanup')
def execute():
    with (output / 'runner.log').open('w', encoding='utf-8') as stream:
        subprocess.run(command, check=True, stdout=stream, stderr=subprocess.STDOUT)
journal = StageJournal(output / 'stages.json')
journal.recover(cleanup)
try:
    journal.run('round2-ready-steam', {'command':command, 'scenario':old['specification']['scenario']}, dependencies, report, execute, cleanup, output / 'resources.json', experiment={
        'first_divergent_boundary':'Sandbox Steam -applaunch produced no game; its updater had failed on inherited movie files.',
        'competing_explanations':['Direct sandbox game launch lacked Steam launch context.', 'Sandbox client startup prevents game connection independently of the rollback runtime.'],
        'new_evidence':'Repair inherited sandbox movie files; Steam update and successful logon now observed. Use direct sandbox game launch with the running client, retaining host Steam launch and current native admission/task instrumentation.',
        'stop_condition':'First native/setup failure begins cleanup; otherwise retain exact round-two correction and same-process re-entry requirements.'})
except Exception as error:
    print(type(error).__name__, str(error)[:300])
    if report.exists():
        result=json.loads(report.read_text())
        print(json.dumps({key:result.get(key) for key in ('result','reason','root_failure','cleanup')},indent=2))
    sys.exit(2)
print('stage completed', report)
