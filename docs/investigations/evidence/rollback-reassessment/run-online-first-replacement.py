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
output = evidence / 'online-first-replacement'
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
    journal.run('round2-first-replacement', {'command':command, 'scenario':old['specification']['scenario']}, dependencies, report, execute, cleanup, output / 'resources.json', experiment={
        'first_divergent_boundary':'Online native repeat at coordinate 360 loses canonical history and requests generation replacement; the terminal diagnostic was erased before root failure.',
        'competing_explanations':['Landing checkpoint detects callback or camera owner replacement.', 'Canonical adapter detects another native identity replacement.'],
        'new_evidence':'Preserve first replacement at every detector before binding release, including camera backing identity; log bounded native input-recorder state at baseline and failure.',
        'stop_condition':'First native/setup failure begins cleanup; otherwise retain exact round-two correction and same-process re-entry requirements.'
})
except Exception as error:
    print(type(error).__name__, str(error)[:300])
    if report.exists():
        result=json.loads(report.read_text())
        print(json.dumps({key:result.get(key) for key in ('result','failure','cleanup')},indent=2))
    sys.exit(2)
print('stage completed', report)
