"""Read-only parsing of recorded native predicates; never decides acceptance."""
from __future__ import annotations
import re

IDENTITY = re.compile(r'\brun_id=([^\s]+)')
FIELDS = re.compile(r'\b(participant|component|check|reason|code|tick|target|cycle|cycles|revision|generation|phase)=([^\s]+)')


def diagnostic(line, *, run_id=None, line_number=None, byte_offset=None):
    if re.fullmatch(r'\s*100% tests passed, 0 tests failed out of \d+\s*', line):
        return None
    fields = dict(FIELDS.findall(line))
    code = fields.get('code', '')
    recovery = bool(re.search(r'\b(?:undo|recovery)\b', line, re.I))
    # Producer probes may deliberately reject without failing the experiment.
    if ('read_only=true' in line or 'unchanged=true' in line) and 'run failed' not in line:
        return None
    failed = bool(re.search(r'\bfailed\b|(?:^|[_=])\w*failed\b', line))
    ground_update = '[HorseMod] ground update admission rejected ' in line
    rejected = ground_update or bool(re.search(r'\brejected\b', line)) and code.isdigit() and int(code) > 0
    hard = bool(re.search(r'AssertionError|Assertion failed|error C\d+|fatal error|checkpoint capacity participant=', line))
    if not (failed or rejected or hard or recovery and code == '18'):
        return None
    if code == '0' and not hard:
        return None
    category = 'unknown'
    for name, pattern in [('recovery', r'recovery|undo'), ('cleanup', r'cleanup'),
                          ('retirement', r'retir'), ('completion', r'complet|settle'),
                          ('publication', r'public|publish'), ('preparation', r'prepar|preflight|reconstruction inventory rejected'),
                          ('capture', r'capture|checkpoint'), ('execution', r'execut|rolling|scheduler')]:
        if re.search(pattern, line, re.I):
            category = name
            break
    fields['participant'] = fields.pop('component', fields.get('participant', 'unknown'))
    if ground_update:
        fields['participant'] = 'ground_update'
        category = 'execution'
    for key, value in list(fields.items()):
        if key not in ('participant', 'check', 'reason', 'phase') and value.isdigit():
            fields[key] = int(value)
    if 'reason' in fields and 'check' not in fields: fields['check'] = fields['reason']
    fields.update(category=category, excerpt=line.strip()[:2048])
    if run_id is not None: fields['run_id'] = run_id
    if line_number is not None: fields['line_number'] = line_number
    if byte_offset is not None: fields['byte_offset'] = byte_offset
    return fields


def scan_lines(lines, run_id, *, offset=0, initial_identity=None, first_line=1):
    """Attribute unlabelled lines only to the last witnessed identity."""
    active = initial_identity
    for number, raw in enumerate(lines, first_line):
        line = raw.decode('utf-8', errors='replace')
        marker = IDENTITY.search(line)
        if marker: active = marker[1]
        event = diagnostic(line, run_id=active, line_number=number, byte_offset=offset)
        if event and run_id and active == run_id:
            yield event
        offset += len(raw)


def scan_new_ground_rejections(path, run_id, state):
    """Consume every appended complete line, retaining only a bounded cursor.

    A tail alone can lose both the identity marker and the first rejection.
    Recheck the file identity, prefix and cursor witness before carrying an
    attribution across polls; replacement/truncation starts a new stream.
    """
    with path.open('rb') as log:
        import os
        stat = os.fstat(log.fileno())
        identity = (stat.st_dev, stat.st_ino)
        prefix = log.read(256)
        offset = state.get('offset', 0)
        witness = state.get('witness', b'')
        log.seek(max(0, offset - len(witness)))
        if (state.get('file') != identity or stat.st_size < offset
                or not prefix.startswith(state.get('prefix', b''))
                or log.read(len(witness)) != witness):
            state.clear()
            offset = 0
        state.update(file=identity, prefix=prefix)
        log.seek(offset)
        while True:
            raw = log.readline(65537)
            if not raw:
                break
            if len(raw) > 65536:
                raise RuntimeError('native diagnostic line exceeds bounded reader capacity')
            if not raw.endswith(b'\n'):
                break # Retry the incomplete line at the next poll.
            offset += len(raw)
            state.update(offset=offset, witness=raw[-64:])
            line = raw.decode('utf-8', errors='replace')
            marker = IDENTITY.search(line)
            if marker:
                state['active'] = marker[1]
            if ('[HorseMod] ground update admission rejected ' in line
                    and state.get('active') == run_id):
                yield diagnostic(line, run_id=run_id, byte_offset=offset-len(raw))
