"""Actual startup/native entries; forwarding observation, never enforcement."""
import json
import os
from pathlib import Path
import re
import subprocess
import pytest
from tools.deterministic_qualification.fixture_cache import run_compile


# Independent acceptance bounds for the measured window, not production imports.
VFX_EVENT_CAP = 512
VFX_SIDECAR_BYTES = 4096 + 512 * 4096


@pytest.mark.native_contract
@pytest.mark.parametrize('vfx_observer_executable', ['stub', 'real-material'], indirect=True)
@pytest.mark.parametrize('case, expected', [('', [True, False]), ('-both', [True, True]), ('-none', [False, False])])
def test_collection18_selected_row_dispatch(tmp_path, vfx_observer_executable, case, expected):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-correlation-slots-row'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    (tmp_path/'native-row-dispatch.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    assert child.returncode == 0, child.stdout+child.stderr
    if 'actual x64Detour' in child.stdout:
        assert 'actual x64Detour rva=1f05150 installed=1 patch_bytes=11' in child.stdout
    count = sum(expected)
    assert f'C18 row native setters=2 changed={count} equal={2-count} dispatches={count} rows_released=1' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    m = value['collection18_witness']['start_helper_correlation']['material_calls']
    assert m['complete'] and [r['family'] for r in m['calls']] == ['get', 'set', 'get', 'set']
    d = m.get('row_dispatch')
    assert isinstance(d, dict), 'selected setter changed/no-op paths have no dispatcher receipt'
    assert d['complete'] and d['pending'] == d['omitted'] == 0
    assert [s['cpu_row_write_branch'] for s in d['setters']] == expected
    assert len(d['calls']) == count
    setters = [r for r in m['calls'] if r['family'] == 'set']
    for r, setter in zip(d['calls'], [s for s, changed in zip(setters, expected) if changed]):
        assert r['setter_id'] == setter['id'] and r['getter_id'] == setter['getter_id']
        assert r['helper_id'] == setter['helper_id'] and r['start_id'] == setter['start_id']
        assert r['matched'] and r['returned'] and r['caller_rva'] == 0x1f1e528
        assert setter['entry_sequence'] < r['entry_sequence'] < r['return_sequence'] < setter['return_sequence']
        assert r['mid'] == setter['mid'] and r['fname'] == setter['fname'] and r['color_bits'] == setter['color_bits']
        assert 'row_pointer' not in r
    from deterministic_qualification.replay_control import _collection18_start_helper_correlation
    assert _collection18_start_helper_correlation(value)
    from copy import deepcopy
    # Old sidecars remain readable and cannot acquire a dispatch proof.
    old = deepcopy(value)
    del old['collection18_witness']['start_helper_correlation']['material_calls']['row_dispatch']
    assert 'row_dispatch' not in _collection18_start_helper_correlation(old)['material_calls']
    changes = ['outcome', 'pending', 'ready', 'complete', 'count', 'setter-count', 'ownership', 'proxy', 'render', 'gpu', 'tick']
    if count:
        changes += ['setter', 'getter', 'helper', 'start', 'parent', 'thread', 'return', 'caller', 'mid', 'fname', 'bits', 'stable', 'pointer']
    for change in changes:
        bad = deepcopy(value)
        bd = bad['collection18_witness']['start_helper_correlation']['material_calls']['row_dispatch']
        r = bd['calls'][0] if count else None
        if change == 'outcome': bd['setters'][-1]['cpu_row_write_branch'] = not expected[-1]
        elif change == 'pending': bd['pending'] += 1
        elif change == 'ready': bd['ready'] = False
        elif change == 'complete': bd['complete'] = False
        elif change == 'count': bd['attempted'] += 1
        elif change == 'setter-count': bd['setters'][0]['matched_dispatches'] += 1
        elif change == 'ownership': bd['ownership_proven'] = True
        elif change in ('proxy', 'render', 'gpu'): bd[change+'_completion_proven'] = True
        elif change == 'tick': bd['native_tick'] = 172
        elif change in ('setter', 'getter', 'helper', 'start'): r[change+'_id'] += 1000
        elif change == 'parent': r['parent_dispatch_id'] = r['id']
        elif change == 'thread': r['thread'] += 1
        elif change == 'return': r['return_sequence'] = setters[0]['return_sequence']+1
        elif change == 'caller': r['caller_rva'] = 0x1f1a5aa
        elif change == 'mid': r['mid'] += 8
        elif change == 'fname': r['fname'] ^= 1 << 32
        elif change == 'bits': r['color_bits'][0] ^= 1
        elif change == 'stable': r['row_stable'] = False
        else: r['row_pointer'] = 1234
        with pytest.raises(RuntimeError, match='correlation differs'):
            _collection18_start_helper_correlation(bad)
    print(child.stdout)


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['republisher', 'mid', 'fname', 'color', 'unreadable', 'unstable', 'reentry',
                                  'concurrent', 'orphan', 'multi', 'overflow', 'invalidate', 'close', 'signature', 'install', 'scheme', 'patch'])
def test_collection18_row_dispatch_fail_closed(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-correlation-slots-row-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    (tmp_path/'native-row-dispatch.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    assert child.returncode == 0, child.stdout+child.stderr
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    native_count = 65 if case == 'overflow' else 2 if case in ('reentry', 'concurrent', 'orphan', 'multi') else 1
    assert f'C18 row native setters=2 changed=1 equal=1 dispatches={native_count} rows_released=1' in child.stdout
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    m = value['collection18_witness']['start_helper_correlation']['material_calls']
    d = m['row_dispatch']
    assert d['complete'] is (case == 'multi') and d['pending'] == 0
    assert d['ready'] is (case not in ('signature', 'install', 'span', 'scheme', 'patch'))
    if case == 'multi':
        assert [s['cpu_row_write_branch'] for s in d['setters']] == [True, False]
    else:
        assert all(s['cpu_row_write_branch'] is None for s in d['setters'])
    if case == 'signature':
        assert not m['calls'] and not d['calls']
    elif case in ('install', 'span'):
        assert len(m['calls']) == 4 and not d['calls']
    elif case == 'overflow':
        assert d['attempted'] == 65 and len(d['calls']) == 64 and d['omitted'] == 1
    elif case == 'republisher':
        assert d['calls'][0]['caller_rva'] == 0x1f1a5aa and not d['calls'][0]['matched']
    elif case == 'reentry':
        assert d['reentry'] and d['calls'][1]['parent_dispatch_id'] == d['calls'][0]['id']
    elif case == 'concurrent':
        assert d['foreign_entries'] == 1 and len(d['calls']) == 1
    elif case == 'orphan':
        assert d['unattributed'] == 1 and d['attempted'] == 2 and len(d['calls']) == 1
    elif case == 'unreadable':
        assert not d['calls'][0]['row_read'] and not d['calls'][0]['matched']
    elif case == 'unstable':
        assert d['calls'][0]['row_read'] and not d['calls'][0]['row_stable']
    elif case == 'multi':
        assert d['setters'][0]['observed_dispatches'] == d['setters'][0]['matched_dispatches'] == 2
        assert len({r['id'] for r in d['calls']}) == 2
    elif case == 'close':
        pending = json.loads((tmp_path/'row_dispatch_pending.json').read_bytes())
        pd = pending['collection18_witness']['start_helper_correlation']['material_calls']['row_dispatch']
        assert not pd['complete'] and pd['pending'] == 1 and not pd['calls'][0]['returned']
        assert all(s['cpu_row_write_branch'] is None for s in pd['setters'])
    from deterministic_qualification.replay_control import _collection18_start_helper_correlation
    assert _collection18_start_helper_correlation(value)
    print(child.stdout)


@pytest.mark.native_contract
@pytest.mark.parametrize('vfx_observer_executable', ['real-material'], indirect=True)
@pytest.mark.parametrize('case', ['span', 'install', 'republisher', 'reentry', 'concurrent', 'close'])
def test_collection18_row_dispatch_real_detour_controls(tmp_path, vfx_observer_executable, case):
    test_collection18_row_dispatch_fail_closed(tmp_path, vfx_observer_executable, case)


def test_collection18_row_dispatch_legacy_live_receipt():
    import hashlib
    import replay_test as module
    from deterministic_qualification.replay_control import _collection18_start_helper_correlation
    digest = '3d0a33dc9d253dd471bac71cf438897854557128470eca0a5e9435f825ccb396'
    path = module.OUTPUT/'evidence/objects'/f'{digest}.json'
    if not path.is_file():
        pytest.skip('requires retained 2026-09-27 selected getter/setter live sidecar')
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == digest
    m = _collection18_start_helper_correlation(json.loads(raw))['material_calls']
    assert m['complete'] and m['vector_row_write_proven'] is False and 'row_dispatch' not in m
    assert len([r for r in m['calls'] if r['family'] == 'set' and r['getter_linked']]) == 2


@pytest.mark.native_contract
@pytest.mark.parametrize('vfx_observer_executable', ['stub', 'real-material'], indirect=True)
def test_collection18_actual_material_slot_returns_and_setter(tmp_path, vfx_observer_executable):
    child = subprocess.run([str(vfx_observer_executable),
                            'collection18-material-shape-zero-correlation-slots'],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    if 'actual x64Detour' in child.stdout:
        assert 'actual x64Detour rva=1dc8220 installed=1 patch_bytes=10' in child.stdout
        assert 'actual x64Detour rva=1f1e420 installed=1 patch_bytes=11' in child.stdout
        assert 'actual getter trampoline JS/JGE/fallthrough PASS' in child.stdout
    native = re.search(r'C18 material slots native count=3 getter=2 setter=1 actual_mid=(\d+)', child.stdout)
    assert native, child.stdout
    actual_mid = int(native[1])
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witness = value['collection18_witness']
    c = witness['start_helper_correlation']
    assert c['complete'] and len(c['calls']) == 2
    assert witness['zero_serial_diagnostic']['rows'][0]['material']['address'] != actual_mid
    receipt = c.get('material_calls')
    assert isinstance(receipt, dict), 'actual GetMaterial return and MID setter are absent despite complete helper correlation'
    assert receipt['complete'] and receipt['pending'] == 0 and receipt['omitted'] == 0
    rows = receipt['calls']
    assert receipt['count_calls_observed'] is False
    assert [r['family'] for r in rows] == ['get', 'set', 'get']
    assert all(r['helper_id'] == c['calls'][1]['id'] and r['start_id'] == c['calls'][0]['id']
               and r['returned'] and r['entry_sequence'] < r['return_sequence'] for r in rows)
    assert [r['slot'] for r in rows if r['family'] == 'get'] == [0, 1]
    assert [r['result'] for r in rows if r['family'] == 'get'] == [actual_mid, 0]
    setter = rows[1]
    assert setter['getter_linked'] and setter['getter_id'] == rows[0]['id'] and setter['slot'] == 0
    assert rows[0]['caller_rva'] == rows[2]['caller_rva'] == 0x8d5892
    assert setter['caller_rva'] == 0x1f45957 and setter['wrapper_caller_rva'] == 0x8d58dd
    assert setter['mid'] == actual_mid and setter['fname'] == c['calls'][1]['fname']
    assert setter['color_bits'] == c['calls'][1]['color_bits']
    assert receipt['ownership_proven'] is False and receipt['vector_row_write_proven'] is False
    print(child.stdout)
    from deterministic_qualification.replay_control import _collection18_start_helper_correlation
    assert _collection18_start_helper_correlation(value) == c
    from copy import deepcopy
    for change in ('helper', 'start', 'parent', 'return', 'pointer', 'slot', 'caller', 'wrapper', 'fname', 'bits',
                   'link', 'pending', 'complete', 'ownership', 'mutation', 'source', 'tick'):
        bad = deepcopy(value)
        m = bad['collection18_witness']['start_helper_correlation']['material_calls']
        s = m['calls'][1]
        if change == 'helper': s['helper_id'] = 999
        elif change == 'start': s['start_id'] = 999
        elif change == 'parent': s['parent_call_id'] = m['calls'][0]['id']
        elif change == 'return': s['return_sequence'] = c['calls'][1]['return_sequence']+1
        elif change == 'pointer': s['mid'] += 8
        elif change == 'slot': s['slot'] = 1
        elif change == 'caller': s['caller_rva'] += 1
        elif change == 'wrapper': s['wrapper_caller_rva'] += 1
        elif change == 'fname': s['fname'] += 1
        elif change == 'bits': s['color_bits'][0] ^= 1
        elif change == 'link': s['getter_id'] = m['calls'][2]['id']
        elif change == 'pending': m['pending'] = 1
        elif change == 'complete': m['complete'] = False
        elif change == 'ownership': m['ownership_proven'] = True
        elif change == 'mutation': m['vector_row_write_proven'] = True
        elif change == 'source': m['material_source'] = 'override'
        else: m['native_tick'] = 172
        with pytest.raises(RuntimeError, match='correlation differs'):
            _collection18_start_helper_correlation(bad)


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['alias', 'reentry', 'concurrent', 'caller', 'mid', 'name', 'color', 'overflow',
                                  'invalidate', 'close', 'revoked', 'signature-get', 'signature-set',
                                  'signature-wrapper', 'table', 'hook-get', 'hook-set'])
def test_collection18_material_slot_fail_closed(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-correlation-slots-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout+child.stderr
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    count = 132 if case == 'overflow' else 3 if case in ('reentry', 'concurrent') else 2
    sets = 2 if case == 'alias' else 1
    assert f'C18 material slots native count=3 getter={count} setter={sets}' in child.stdout
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    c = value['collection18_witness']['start_helper_correlation']
    m = c['material_calls']
    assert m['complete'] is (case == 'revoked')
    assert m['pending'] == 0
    startup_failure = case.startswith('signature-') or case in ('table', 'hook-get', 'hook-set', 'span')
    assert m['ready'] is (not startup_failure)
    if startup_failure:
        assert len(m['calls']) == (2 if case == 'hook-set' else 0)
        assert c['complete']  # New optional failure never changes the first receipt or veto coverage.
    elif case == 'overflow':
        assert len(m['calls']) == 128 and m['omitted'] == 5
    elif case == 'reentry':
        assert m['reentry'] and m['calls'][1]['parent_call_id'] == m['calls'][0]['id']
    elif case == 'concurrent':
        assert m['foreign_entries'] == 1 and len(m['calls']) == 3
    elif case not in ('invalidate', 'close', 'revoked'):
        assert m['invalidated']
    from deterministic_qualification.replay_control import _collection18_start_helper_correlation
    assert _collection18_start_helper_correlation(value) == c
    print(child.stdout)


@pytest.mark.native_contract
@pytest.mark.parametrize('vfx_observer_executable', ['real-material'], indirect=True)
@pytest.mark.parametrize('case', ['span', 'hook-set'])
def test_collection18_material_detour_rejects_expansion_and_partial_install(tmp_path, vfx_observer_executable, case):
    test_collection18_material_slot_fail_closed(tmp_path, vfx_observer_executable, case)


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['normal', 'no-start', 'no-helper', 'reentry', 'thread', 'orphan', 'overflow',
                                  'unreadable', 'link-mismatch', 'close', 'invalidate', 'callback-reentry', 'concurrent'])
def test_collection18_start_helper_correlation(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable),
                            'collection18-material-shape-zero-correlation-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    native = re.search(r'C18 correlation native starts=(\d+) helpers=(\d+) component=(\d+) actor=(\d+) provider=(\d+) name=(\d+)', child.stdout)
    assert native, child.stdout
    starts, helpers, component, actor, provider, name = map(int, native.groups())
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witness = value['collection18_witness']
    receipt = witness.get('start_helper_correlation')
    assert isinstance(receipt, dict), 'selected C18 forwards Start/helper but loses its argument/ancestry receipt'
    assert receipt['version'] == 1 and receipt['c18_entry_sequence'] == witness['entry_sequence']
    assert receipt['thread'] == witness['thread'] and receipt['ended']
    assert receipt['attempted'] == starts+helpers and receipt['pending'] == 0
    assert receipt['complete'] is (case in ('normal', 'no-start', 'no-helper', 'reentry', 'link-mismatch'))
    assert receipt['omitted'] == max(0, starts+helpers-64)
    assert receipt['native_tick'] is None and receipt['mid_slot'] is None
    assert receipt['ownership_proven'] is False and receipt['mid_writes_observed'] is False
    rows = receipt['calls']
    assert len(rows) == min(64, starts+helpers)
    by_id = {r['id']: r for r in rows}
    for ordinal, row in enumerate(rows, 1):
        assert row['ordinal'] == ordinal and row['returned']
        assert row['entry_sequence'] < row['return_sequence']
        assert row['on_selected_thread'] is (row['thread'] == witness['thread'])
        if row['family'] == 'start':
            assert row['component'] == component and row['request'] == witness['descriptor']
        else:
            assert row['actor'] == actor and row['provider'] == provider
            assert row['provider_link_matches'] is (case != 'link-mismatch')
            assert row['fname'] == name
            assert row['color_bits'] == ([0]*4 if case == 'unreadable' else [0x80000000, 0x3f800000, 0x7fc01234, 0x3e800000])
            assert row['color_read'] is (case != 'unreadable') and row['color_stable'] is (case != 'unreadable')
            if case != 'orphan':
                assert by_id[row['start_id']]['component'] == component
                if case not in ('link-mismatch', 'close', 'callback-reentry'):
                    assert row['entry_provider_matches']
                else:
                    assert not row['entry_provider_matches']
        if row['parent_id']:
            parent = by_id[row['parent_id']]
            assert parent['thread'] == row['thread'] and parent['entry_sequence'] < row['entry_sequence']
            assert row['return_sequence'] < parent['return_sequence']
    assert value['owned_bytes'] == 4*1024*1024
    if case == 'concurrent':
        assert {r['on_selected_thread'] for r in rows} == {False, True}
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_start_helper_correlation'] == receipt
    if case == 'normal':
        from copy import deepcopy
        path = tmp_path/'vfx_completion_observation.json'
        for corrupt in ('parent', 'start', 'thread', 'name', 'bits', 'return', 'pending', 'count', 'permission', 'native-tick'):
            bad = deepcopy(value)
            c = bad['collection18_witness']['start_helper_correlation']
            r = c['calls'][1]
            if corrupt == 'parent': r['parent_id'] = 0
            elif corrupt == 'start': r['start_id'] = 999
            elif corrupt == 'thread': r['thread'] += 1
            elif corrupt == 'name': r['fname'] = -1
            elif corrupt == 'bits': r['color_bits'][0] = 2**32
            elif corrupt == 'return': r['return_sequence'] = c['calls'][0]['return_sequence']+1
            elif corrupt == 'pending': c['pending'] = 1
            elif corrupt == 'count': c['attempted'] += 1
            elif corrupt == 'permission': c['ownership_proven'] = True
            else: c['native_tick'] = 172
            path.write_text(json.dumps(bad))
            retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in retained, corrupt
            assert 'vfx_collection18_start_helper_correlation' not in retained, corrupt
        path.write_text(json.dumps(value))
        path.with_name(path.name+'.tmp').write_text('interrupted')
        retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
        assert 'vfx_collection18_start_helper_correlation' not in retained


@pytest.mark.parametrize('changed', [None, 'function', 'scheduled_task', 'completion',
                                     'world', 'serial', 'thread', 'epoch', 'scope_id',
                                     'later', 'unreadable', 'truncated', 'missing_first'])
def test_retained_first_trace_continuation_correlation(tmp_path, changed):
    """Replay the actual first failure; no fixture grants native task ownership."""
    import hashlib
    import replay_test as module
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    digest = '7840e51724420a6ce1584d28b8e56f2c74057f7e7b7f16c1d5cdda6e07776910'
    source = module.OUTPUT/'evidence/objects'/f'{digest}.json'
    if not source.is_file():
        pytest.skip('requires immutable 2026-09-24 first-failure sidecar')
    raw = source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == digest
    value = json.loads(raw)
    producer = next(p for p in value['producers'] if p['id'] == 67)
    entry = next(o for o in producer['execution_owners'] if o['kind'] == 2)
    if changed in ('function', 'scheduled_task', 'completion', 'world'):
        entry[changed] += 8
    elif changed == 'serial': entry['tick_owner']['serial'] += 1
    elif changed == 'thread': producer['thread'] += 1
    elif changed == 'epoch': producer['observed_application_epoch'] += 1
    elif changed == 'scope_id': entry['id'] = 57429
    elif changed == 'later': producer['entry_ms'] = producer['return_ms'] = 2629651079
    elif changed == 'unreadable': entry['task_read'] = False
    elif changed == 'truncated': producer['owners_truncated'] = True
    elif changed == 'missing_first': value['registration_ancestry'].pop(0)
    path = tmp_path/'vfx.json'
    path.write_text(json.dumps(value))
    report = {}
    retain_vfx_completion_observation(report, path, value['run_id'], tmp_path/'objects')
    # Invalid whole-sidecar relationships must not yield a correlation receipt.
    if changed in ('thread', 'later', 'truncated', 'missing_first'):
        assert 'vfx_completion_observation_collection_error' in report
        assert 'vfx_first_trace_writer' not in report
        return
    assert 'vfx_completion_observation_collection_error' not in report, report
    receipt = report.get('vfx_first_trace_writer')
    assert receipt is not None, 'first kind3 writer loses the retained kind2 correlation'
    assert receipt['pair'] == 2 and receipt['scope_id'] == 57428
    assert receipt['result'] == 'no_go' and receipt['reason'] == 'continuation_not_entry'
    assert receipt['earlier_entry_snapshots'] == ([{'producer_id': 67, 'scope_id': 57422,
        'snapshot_boundary': 'producer_entry'}] if changed is None else [])
    assert receipt['allocation_generation_recorded'] is False
    assert receipt['hold_resume_transition_recorded'] is False
    assert receipt['ownership_proven'] is False and receipt['before_trace_effects_proven'] is False
    # All later registration rows remain present; none can replace pair2.
    assert len(value['registration_ancestry']) == 16
    if changed is None:
        # A later corrupt publication cannot leave an old correlation attached
        # to a report reused by the existing retention caller.
        value['run_id'] = 'wrong-run'
        path.write_text(json.dumps(value))
        retain_vfx_completion_observation(report, path, 'original-run', tmp_path/'objects')
        assert 'vfx_first_trace_writer' not in report


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['', '-generation', '-thread', '-custom-entry', '-custom-resume',
                                  '-nested', '-overflow', '-close', '-capacity'])
def test_vfx_registration_pre_payload_ancestry(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'producers-owners-registration'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    print(child.stdout + child.stderr)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'registration inside native task forwarded' in child.stdout
    assert 'producer forwarding PASS' in child.stdout
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witnesses = report.get('registration_ancestry')
    assert witnesses is not None, 'native registration forwarded without pre-payload ancestry'
    assert len(witnesses) == (16 if case == '-capacity' else 2)
    assert witnesses[0]['reason'] == 'missing_scope'
    w = witnesses[1]
    assert w['returned'] and w['ancestry'] and w['entry_sequence'] < w['return_sequence']
    assert not w['ownership_proven'] and not w['before_trace_effects_proven']
    if case == '-thread':
        assert w['scopes'] == [] and w['reason'] == 'missing_scope'
    elif case == '-overflow':
        assert w['reason'] == 'scope_capacity' and w['scopes_truncated']
        assert report['registration_scope_overflow'] > 0
        assert all(not s['read'] and not s['returned'] and not s['entry_sequence'] for s in w['scopes'])
    else:
        assert len(w['scopes']) == (2 if case in ('-nested', '-overflow') else 1)
        s = w['scopes'][0]
        assert s['kind'] == {'-custom-entry': 2, '-custom-resume': 3}.get(case, 1) and s['returned']
        assert s['task'] == s['scheduled_task'] and s['task'] > 0
        assert s['owner']['index'] == 7 and s['owner']['serial'] == 11
        assert s['entry_sequence'] < w['entry_sequence'] < w['return_sequence'] < s['return_sequence']
        assert s['thread'] == w['thread'] and s['native_thread'] == 2
        assert s['identity_matches_at_registration'] is (case != '-generation')
        assert s['registration_owner_serial'] == (12 if case == '-generation' else 11)
        assert w['reason'] == {'-generation': 'identity_changed', '-custom-resume': 'continuation_not_entry',
                               '-nested': 'overlapping_or_nested_scope', '-overflow': 'scope_capacity'}.get(case, 'correlation_only')
    assert report['owned_bytes'] == 4*1024*1024
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    path = tmp_path/'vfx_completion_observation.json'
    retained = {}
    retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_completion_observation_phase_complete'] is (case not in ('-overflow', '-close', '-capacity'))
    if case == '-close': assert report['registration_pending_at_close']
    if case == '-capacity': assert report['registration_scope_overflow'] == 2
    if not case:
        from copy import deepcopy
        for corruption in ('version', 'ownership', 'pair', 'thread', 'generation', 'sequence', 'scope-return'):
            bad = deepcopy(report); b = bad['registration_ancestry'][1]; s = b['scopes'][0]
            if corruption == 'version': bad.pop('registration_ancestry_version')
            elif corruption == 'ownership': b['ownership_proven'] = True
            elif corruption == 'pair': b['pair'] = 99999
            elif corruption == 'thread': s['thread'] += 1
            elif corruption == 'generation': s['registration_owner_serial'] += 1
            elif corruption == 'sequence': s['entry_sequence'] = b['entry_sequence']
            else: s['return_sequence'] = b['return_sequence']
            path.write_text(json.dumps(bad)); rejected = {}
            retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, corruption
        path.write_text(json.dumps(report))


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['', '-table', '-code', '-stale', '-storage', '-inline'])
def test_vfx_hub_onearg_native_layout(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'producers-owners-hub-onearg'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    print(child.stdout + child.stderr)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'producer forwarding PASS' in child.stdout
    path = tmp_path/'vfx_completion_observation.json'
    report = json.loads(path.read_bytes())
    hub = report['disable_hub_witness']
    row = hub['rows'][1]
    assert hub['selected'] and hub['returned']
    assert row['table'] == hub['image_base']+0x374b760
    assert row['producer_id'] == 0  # A readable row does not prove invocation.
    if not case:
        assert row['state'] == 'known', 'forwarded native hub lacks one-argument receiver witness'
        assert row['callable'] == hub['image_base']+0x4704f0
        assert row['receiver']['index'] == row['weak_index'] == 11
        assert row['receiver']['serial'] == row['weak_serial'] == 71
        assert row['handle'] == 0x456 and row['bound_byte'] is None
        assert hub['snapshot_complete'] and report['phase']['complete']
    else:
        assert row['state'] == {'-table':'unknown_layout','-code':'unknown_layout',
                                '-stale':'unknown_receiver','-storage':'unknown_storage','-inline':'unknown_storage'}[case]
        assert not hub['snapshot_complete'] and not report['phase']['complete']
    assert not hub['writer_coverage_proven'] and not hub['receiver_undo_proven']
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_completion_observation_phase_complete'] is (not case)
    if not case:
        from copy import deepcopy
        for corruption in ('bound', 'correlation', 'table', 'layout-version', 'missing-bound'):
            bad = deepcopy(report); r = bad['disable_hub_witness']['rows'][1]
            if corruption == 'bound': r['bound_byte'] = 0
            elif corruption == 'correlation': r['producer_id'] = hub['rows'][0]['producer_id']
            elif corruption == 'layout-version': bad.pop('disable_hub_onearg_layout_version')
            elif corruption == 'missing-bound': r.pop('bound_byte')
            else: r['table'] += 1
            path.write_text(json.dumps(bad)); rejected = {}
            retain_vfx_completion_observation(rejected,path,'vfx-observation-fixture',tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, corruption
        path.write_text(json.dumps(report))
        # Version-one historical unknown rows retain their numeric placeholder.
        # Never retroactively turn that missing payload into a known receiver.
        legacy = deepcopy(report); legacy.pop('disable_hub_onearg_layout_version')
        legacy['disable_hub_witness']['rows'][1]['state'] = 'unknown_layout'
        legacy['disable_hub_witness']['rows'][1]['bound_byte'] = 0
        legacy['disable_hub_witness']['snapshot_complete'] = False
        legacy['phase']['complete'] = False
        path.write_text(json.dumps(legacy)); retained = {}
        retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
        assert 'vfx_completion_observation_collection_error' not in retained, retained
        assert retained['vfx_completion_observation_phase_complete'] is False
        path.write_text(json.dumps(report))


@pytest.mark.native_contract
def test_vfx_hub_two_entry_witness_before_forwarding(tmp_path, vfx_observer_executable):
    child = subprocess.run([str(vfx_observer_executable), 'producers-owners-hub'],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    print(child.stdout + child.stderr)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'producer forwarding PASS' in child.stdout
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    hub = report.get('disable_hub_witness')
    assert hub is not None, 'native forwarded without the two-entry hub witness'
    assert hub['selected'] and hub['returned'] and hub['snapshot_complete']
    assert hub['snapshot_boundary'] == 'before_original_140400590'
    assert hub['count'] == 2 and hub['capacity'] == 18 and hub['recursion'] == 0
    assert [r['receiver']['index'] for r in hub['rows']] == [7, 11]
    assert [r['weak_serial'] for r in hub['rows']] == [11, 71]
    assert [r['handle'] for r in hub['rows']] == [0x123, 0x456]
    assert all(r['state'] == 'known' and r['enabled'] == 3 and r['bound_byte'] == 1 for r in hub['rows'])
    assert all(r['table']==hub['image_base']+0x326bfe8 and r['callable']==hub['image_base']+0x3c5250 for r in hub['rows'])
    assert hub['rows'][1]['entry'] == hub['rows'][0]['entry'] + 0x40
    assert hub['rows'][0]['producer_id'] > 0 and hub['rows'][1]['producer_id'] == 0
    assert hub['scope_kind'] == 2 and hub['scope_id'] > 0
    assert hub['writer_coverage_proven'] is False and hub['receiver_undo_proven'] is False
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained={}
    retain_vfx_completion_observation(retained,tmp_path/'vfx_completion_observation.json','vfx-observation-fixture',tmp_path/'objects')
    assert retained.get('vfx_completion_observation_phase_complete') is True,retained
    from copy import deepcopy
    path=tmp_path/'vfx_completion_observation.json'
    for corruption in ('version','recovery','correlation','unknown','truncated','missing-receiver','table'):
        bad=deepcopy(report);h=bad['disable_hub_witness']
        if corruption=='version':bad.pop('disable_hub_witness_version')
        elif corruption=='recovery':h['receiver_undo_proven']=True
        elif corruption=='correlation':h['rows'][0]['producer_id']=999999
        elif corruption=='unknown':h['rows'][1]['state']='unknown_layout'
        elif corruption=='missing-receiver':h['rows'][0]['producer_id']=0
        elif corruption=='table':h['rows'][1]['table']+=1
        else:h['truncated']=True
        path.write_text(json.dumps(bad));rejected={}
        retain_vfx_completion_observation(rejected,path,'vfx-observation-fixture',tmp_path/'objects')
        assert 'vfx_completion_observation_collection_error' in rejected,corruption
    path.write_text(json.dumps(report))


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['unreadable','unknown','stale','truncated','unknown-storage','writers','nested','threaded','overflow','close'])
def test_vfx_hub_incomplete_still_forwards(tmp_path, vfx_observer_executable, case):
    child=subprocess.run([str(vfx_observer_executable),'producers-owners-hub-'+case],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'producer forwarding PASS' in child.stdout
    report=json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    hub=report['disable_hub_witness']
    assert hub['selected'] and hub['returned'] and not report['phase']['complete']
    if case=='close':
        assert hub['pending_at_close'] and hub['snapshot_complete']
    else:
        assert not hub['snapshot_complete']
    if case in ('writers','nested','threaded','overflow'):
        writers=hub['writers']
        assert len(writers)=={'writers':2,'nested':3,'threaded':1,'overflow':16}[case]
        assert all(w['returned'] for w in writers)
        if case in ('writers','nested'):
            assert writers[0]['entry_rva']==0x3a3b70 and writers[0]['handle_read'] and writers[0]['handle']==0x789
            assert writers[0]['result_raw']==writers[0]['output_address']
            assert all(w['scopes'][0]==[hub['id'],4] and w['scopes'][1]==[hub['scope_id'],2] for w in writers)
        if case=='nested':assert writers[1]['parent_pair']==1
        if case=='threaded':assert writers[0]['scopes']==[] and not writers[0]['same_thread']
        if case=='overflow':assert hub['writer_overflow']
        assert all(w['result_raw']==0xabcdef0123456789 for w in writers if w['entry_rva']==0x3ca7a0)
    else:
        assert hub['writers']==[]
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained={}
    retain_vfx_completion_observation(retained,tmp_path/'vfx_completion_observation.json','vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_completion_observation_phase_complete'] is False


@pytest.mark.native_contract
@pytest.mark.parametrize('mode', ['producers-owners-overflow','producers-owners-unreadable'])
def test_vfx_execution_witness_limits_still_forward(tmp_path,vfx_observer_executable,mode):
    child=subprocess.run([str(vfx_observer_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'producer forwarding PASS' in child.stdout
    report=json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    if mode.endswith('overflow'):
        assert report['dropped']==2 and not report['phase']['complete']
        assert all(len(p['execution_owners'])==4 and p['owners_truncated'] for p in report['producers'])
    else:
        assert report['phase']['complete']
        outer=next(p for p in report['producers'] if p['entry_rva']==0x3c5250)
        task=outer['execution_owners'][-1]
        assert task['function']==1 and task['task_read'] is False and task['tick_owner']['valid'] is False
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained={}
    retain_vfx_completion_observation(retained,tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained,retained


@pytest.mark.native_contract
@pytest.mark.parametrize('mode,kind', [('producers-owners',1), ('producers-owners-custom-entry',2),
                                      ('producers-owners-custom-resume',3), ('producers-owners-threaded',1)])
def test_vfx_positive_routes_distinguish_execution_owners(tmp_path, vfx_observer_executable, mode, kind):
    child = subprocess.run([str(vfx_observer_executable), mode], cwd=tmp_path,
                           capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    assert 'producer forwarding PASS' in child.stdout
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    outer = next(p for p in report['producers'] if p['entry_rva']==0x3c5250)
    inner = next(p for p in report['producers'] if p['entry_rva']==0x1f73990 and p['thread']==outer['thread'])
    if mode=='producers-owners-threaded':
        unrelated = next(p for p in report['producers'] if p['thread']!=outer['thread'])
        assert unrelated['execution_owners']==[] and unrelated['parent']==0
    # RED is absent identity at the real forwarding entry, not a fake lease.
    assert len(outer.get('execution_owners', [])) == 2, child.stdout
    assert len(inner['execution_owners']) == 3
    hub, task = outer['execution_owners']
    particle, nested_hub, ancestor = inner['execution_owners']
    assert [hub['kind'], task['kind'], particle['kind']] == [4, kind, 1]
    assert nested_hub == hub and ancestor == task
    assert particle['context'] != task['context']
    assert particle['tick_owner']['index'] == 8 and particle['tick_owner']['serial'] == 41
    assert task['tick_owner']['index'] == 7 and task['tick_owner']['serial'] == 11
    for t in (task, particle):
        assert t['task_read'] and t['scheduled_task'] == t['context']
        assert t['completion'] == 0x99880000 and t['argument'] == t['context'] + 0x40
        assert t['native_thread'] == 2
    assert hub['collection'] == hub['context'] + 0x78
    assert hub['hub_read'] and hub['count'] == (2 if kind in (2,3) else 1)
    assert hub['hub_owner']['index']==10 and hub['hub_owner']['serial']==61
    assert all(p['owner_snapshot_boundary'] == 'producer_entry' and not p['owners_truncated']
               and p['ancestry'] for p in (outer, inner))
    assert report['task_attribution_proven'] is False
    assert report['phase']['complete'] is True
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture', tmp_path/'objects')
    assert retained['vfx_completion_observation_phase_complete'] is True, retained
    if mode=='producers-owners':
        from copy import deepcopy
        path=tmp_path/'vfx_completion_observation.json'
        for corruption in ('missing_version','lease','collection','kind','duplicate','overflow'):
            bad=deepcopy(report)
            p=bad['producers'][0]
            if corruption=='missing_version': bad.pop('execution_owner_observation_version')
            elif corruption=='lease': bad['execution_owner_lease_proven']=True
            elif corruption=='collection': p['execution_owners'][0]['collection']+=1
            elif corruption=='kind': p['execution_owners'][0]['kind']=9
            elif corruption=='duplicate': p['execution_owners'][1]['id']=p['execution_owners'][0]['id']
            else: p['owners_truncated']=True
            path.write_text(json.dumps(bad)); rejected={}
            retain_vfx_completion_observation(rejected,path,'vfx-observation-fixture',tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected,corruption
        path.write_text(json.dumps(report))


@pytest.mark.native_contract
@pytest.mark.parametrize('mode', ('producers', 'producers-threaded', 'producers-close', 'producers-crash'))
def test_vfx_pre_effect_producer_attribution(tmp_path, vfx_observer_executable, mode):
    child = subprocess.run([str(vfx_observer_executable), mode], cwd=tmp_path,
                           capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    producers = report.get('producers', [])
    # RED must be missing attribution after actual native forwarding, not a
    # compile/link failure or a fixture-supplied task ownership assertion.
    assert len(producers) == 2, child.stdout
    outer, inner = sorted(producers, key=lambda p: p['id'])
    assert outer['entry_rva'] == 0x3c5250 and inner['entry_rva'] == 0x1f73990
    assert outer['parent'] == 0 and inner['parent'] == outer['id']
    assert outer['bound_byte'] == 1 and outer['object']['index'] == 7
    assert inner['object']['index'] == 8 and inner['object']['serial'] == 41
    assert inner['async_fence'] == 0x12340000 and inner['update_active'] == 1
    assert inner['particle_state_read'] is True
    assert all(p['epoch_read'] and p['observed_application_epoch'] == 901 for p in producers)
    assert report['producer_observation_version'] == 1
    assert report['task_attribution_proven'] is False
    assert report['producer_entry_persisted_before_native'] is False
    selected = [e for e in report['events'] if e['producer_id']]
    assert selected[0]['producer_id'] == inner['id']
    assert all(p['entry_ms'] <= selected[0]['time_ms'] for p in producers)
    if mode == 'producers-crash':
        assert all(not p['returned'] for p in producers)
        assert report['producer_pending'] == 2
    else:
        assert 'producer forwarding PASS' in child.stdout
        assert all(p['returned'] and p['return_ms'] >= p['entry_ms'] for p in producers)
        assert report['producer_pending'] == 0
        assert report['phase']['complete'] is (mode in ('producers', 'producers-threaded'))
        if mode in ('producers', 'producers-threaded'):
            assert [e['producer_id'] for e in selected] == [inner['id']]*2 + [outer['id']]*2
            assert all(e['producer_id'] == 0 for e in report['events'][-2:])
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_completion_observation_phase_complete'] is (mode in ('producers', 'producers-threaded'))
    if mode == 'producers-threaded':
        # This dispatch runs while the outer producer is active on another
        # thread; a global most-recent-producer attribution would be wrong.
        assert all(e['producer_id'] == 0 for e in report['events'][6:8])
    if mode == 'producers':
        from copy import deepcopy
        path = tmp_path/'vfx_completion_observation.json'
        for corruption in ('pending', 'parent', 'thread', 'link', 'pair', 'version', 'task'):
            bad = deepcopy(report)
            if corruption == 'pending': bad['producer_pending'] = 1
            elif corruption == 'parent': bad['producers'][0]['parent'] = bad['producers'][0]['id']
            elif corruption == 'thread': bad['producers'][1]['thread'] += 1
            elif corruption == 'link': bad['events'][-1]['producer_id'] = 999
            elif corruption == 'pair': bad['events'][7]['producer_id'] = 0
            elif corruption == 'version': bad.pop('producer_observation_version')
            else: bad['task_attribution_proven'] = True
            path.write_text(json.dumps(bad)); rejected = {}
            retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
            assert rejected['vfx_completion_observation_phase_complete'] is False, corruption
            assert 'vfx_completion_observation_collection_error' in rejected, corruption
        path.write_text(json.dumps(report))


@pytest.mark.native_contract
def test_vfx_producer_capacity_still_forwards(tmp_path, vfx_observer_executable):
    child = subprocess.run([str(vfx_observer_executable), 'producers-overflow'], cwd=tmp_path,
                           capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    assert len(report['producers']) == 128
    assert report['producer_pending'] == 0 and all(p['returned'] for p in report['producers'])
    assert report['dropped'] == 2 and report['phase']['complete'] is False
    assert child.stdout.count('native-snapshot') == 133  # initial two + 65*2 + outside one
    assert report['owned_bytes'] == 4*1024*1024


@pytest.mark.native_contract
def test_vfx_attachment_broadcast_uses_existing_dispatch_owner(tmp_path, vfx_observer_executable):
    child = subprocess.run([str(vfx_observer_executable), 'producers-attachment'], cwd=tmp_path,
                           capture_output=True, text=True, timeout=30)
    assert child.returncode == 0, child.stdout + child.stderr
    report = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    attached = [e for e in report['events'] if e.get('collection_kind') == 'trace_attachment_deactivated']
    assert len(attached) == 4, child.stdout
    assert all(e['entry_rva'] == 0x3d74d0 and e['manager']['index'] == 9
               and e['manager']['serial'] == 51 and e['producer_id'] > 0 for e in attached)
    pairs = {e['pair']: e for e in report['events'] if e['boundary'] == 'entry'}
    for e in attached:
        parent = pairs[e['parent_pair']]
        assert parent['collection_kind'] == 'vfx_manager_finished'
        assert parent['producer_id'] == e['producer_id'] and parent['depth'] + 1 == e['depth']
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture', tmp_path/'objects')
    assert retained['vfx_completion_observation_phase_complete'] is True, retained


@pytest.fixture(scope='module')
def vfx_observer_executable(tmp_path_factory, request):
    import replay_test as module
    from deterministic_iteration import VCVARS

    tmp_path=tmp_path_factory.mktemp('vfx-completion-native')
    # Compile the sole production owner of141D38300, including its existing
    # input-filter path. The fixture supplies only external native services and
    # storage; it has no substitute collection18 observer or validity predicate.
    source=(module.ROOT/'HorseMod/horselib/deterministic/DeterministicHookSet.FrameAndStage.inl').read_text()
    start=source.index('void __fastcall DeterministicHookSet::CallbackExecutorDetour(')
    end=source.index('void __fastcall DeterministicHookSet::StageBreakWallDetour(',start)
    (tmp_path/'vfx_callback_executor.inl').write_text(source[start:end])
    source=(module.ROOT/'HorseMod/horselib/deterministic/DeterministicHookSet.cpp').read_text()
    start=source.index('bool CaptureInputPairArray(')
    end=source.index('struct MaskedField',start)
    (tmp_path/'vfx_callback_inputs.inl').write_text(source[start:end])
    for suffix,source_name in (('entry','Sc6ReplayTaskGroup.cpp'),('resume','Sc6ReplayTaskGroup.Resume.inl')):
        source=(module.ROOT/'HorseMod/horselib/deterministic'/source_name).read_text()
        start=source.index('ReplayVfxExecutionScope vfx_execution(')
        end=source.index(';',start)+1
        (tmp_path/f'vfx_owner_manager_{suffix}.inl').write_text(source[start:end])
    # An external header cannot acquire the current workspace's source identity.
    assert not os.environ.get('HORSE_VFX_OBSERVER_RED_HEADER'), 'alternate observer header is not source-qualified'
    source=(module.ROOT/'HorseMod/HorseModService.PublicApiAndLifetime.inl').read_text()
    start=source.index('        if(Horse::Deterministic::VerifiedReplayExecutable()) {')
    end=source.index('        if(deterministic_load.status.ok()',start)
    (tmp_path/'vfx_material_startup.inl').write_text(source[start:end])
    start=source.index('            const bool startup=RC::IsInitialCppModStartupThread();',end)
    end=source.index('        }\n        m_replay_native_runtime',start)
    (tmp_path/'vfx_observation_startup.inl').write_text(source[start:end])
    # Execute the actual trajectory-completion diagnostic block and DLL export
    # bodies. Before phase closure exists this still compiles and reaches native
    # shutdown calls; the retention assertion, not a missing symbol, is RED.
    host=(module.ROOT/'tools/replay_qualification_mod/ReplayQualificationMod.cpp').read_text()
    start=host.index('        if(startup_request_valid && startup_request.c18_diagnostic) {')
    end=host.index('        if (ReadRequest(', start)
    (tmp_path/'vfx_c18_diagnostic_startup.inl').write_text(host[start:end])
    start=host.index('    bool ObserveC18CombatSample(')
    end=host.index('    void ObserveReplayTrajectory()', start)
    (tmp_path/'vfx_c18_diagnostic_sample.inl').write_text(host[start:end])
    start=host.index('        if(request_.c18_diagnostic) {',host.index('    void Fail('))
    end=host.index('        const bool first=',start)
    (tmp_path/'vfx_c18_diagnostic_fail.inl').write_text(host[start:end])
    start=host.index('            const auto vfx_',host.index('    void CompleteTrajectory()'))
    end=host.index('            // Observation validity',start)
    (tmp_path/'vfx_observation_completion.inl').write_text(host[start:end])
    # Isolated post-application settlement boundary. Keep the real particle
    # drain/poll/settle methods and host decisions; native query/task scheduling
    # are controlled independently. This does not execute provider admission,
    # trace publication, B reconciliation, retirement, or the rest of the host.
    native_root=module.ROOT/'HorseMod/horselib/deterministic'
    copy=(native_root/'Sc6ReplayParticleCopy.cpp').read_text()
    start=copy.index('bool Sc6ReplayParticleCopy::SettleExecution(')
    end=copy.index('bool Sc6ReplayParticleCopy::DrainPrivateOwnerRetirement()',start)
    methods=copy[start:end]
    start=copy.index('void Sc6ReplayParticleCopy::Poll()')
    end=copy.index('    if(witness_.phase==Phase::ReadPrivateOwnerRetirement)',start)
    (tmp_path/'vfx_material_particle_methods.inl').write_text(methods+copy[start:end]+'}\n')
    header=(native_root/'Sc6ReplayParticleCopy.hpp').read_text()
    start=header.index('    enum class Phase :')
    (tmp_path/'vfx_material_particle_phase.inl').write_text(header[start:header.index(';',start)+1])
    driver=(native_root/'Sc6ReplayHost.Restore.inl').read_text()
    start=driver.index('bool Sc6ReplayHost::CheckHistoricalMaterialBoundary(')
    end=driver.index('Status Sc6ReplayHost::RequestHistoricalRestore(',start)
    (tmp_path/'vfx_material_host_guard.inl').write_text(driver[start:end])
    start=driver.index('Status Sc6ReplayHost::RequestHistoricalRestore(')
    end=driver.index('void Sc6ReplayHost::AdvanceRestorePreparation()',start)
    (tmp_path/'vfx_material_host_request.inl').write_text(driver[start:end])
    start=driver.index('Status Sc6ReplayHost::PrepareHistoricalRestore(')
    end=driver.index('    transaction.prior_mode =',start)
    # Keep every direct-admission check and ownership decision, including the
    # real baseline check. Reaching the unmodelled preparation participants is
    # observed by an exception, never supplied as a successful preparation.
    (tmp_path/'vfx_material_host_prepare.inl').write_text(
        driver[start:end]+'    throw PreparationReached{};\n}\n')
    start=driver.index('bool Sc6ReplayHost::CanReleaseHistoricalRestore(')
    end=driver.index('bool Sc6ReplayHost::HistoricalNativeOwnersReleased(',start)
    (tmp_path/'vfx_material_host_release.inl').write_text(driver[start:end])
    host=(native_root/'Sc6ReplayHost.cpp').read_text()
    start=host.index('\n{',host.index('struct Sc6ReplayHost::HistoricalRestore'))+2
    end=host.index('    struct FreshParticle',start)
    (tmp_path/'vfx_material_receipt_fields.inl').write_text(host[start:end])
    # Exercise the real operation acquisition fragment in isolation, without
    # bypassing HistoricalRestoreSupported or granting a fixture lease. Old
    # source compiles too: it forwards Start and reaches the mutation sentinel.
    start=driver.index('    auto operation=std::make_unique<HistoricalRestore>();')
    end=driver.index('    operation->target=',start)
    (tmp_path/'vfx_trace_start_operation.inl').write_text(driver[start:end])
    start=driver.index('    if(!requested) {',driver.index('Status Sc6ReplayHost::PrepareHistoricalRestore('))
    end=driver.index('    auto& transaction =',start)
    (tmp_path/'vfx_trace_start_direct_operation.inl').write_text(driver[start:end])
    start=driver.index('            if(!execution.retirement.admitted) {',driver.index('if(execution.render==Render::HandedOff)'))
    end=driver.index('            RC::Output::send',start)
    (tmp_path/'vfx_material_host_retirement.inl').write_text(driver[start:end])
    start=driver.index('    if(execution.render==Render::DrainPending) {')
    end=driver.index('    if(execution.render!=Render::Settled)',start)
    (tmp_path/'vfx_material_host_settlement.inl').write_text(driver[start:end])
    surface=(native_root/'Sc6ReplayHost.Surface.inl').read_text()
    start=surface.index('    case ParticleCopyAction::SettleExecution:')
    end=surface.index('    case ParticleCopyAction::ContinueExecutionForUndo:',start)
    (tmp_path/'vfx_material_surface_settlement.inl').write_text(surface[start:end])
    # Corrected-capture regression: retain every decision on the reached
    # capture -> command admission -> render Finish -> restore -> seek path.
    # Unrelated action/phase tails are replaced by aborts, never success stubs.
    # The C++ fixture admits only Finish/Poll/RetireInFlight here; their shared
    # command checks and completion stores are copied verbatim from production.
    public=(native_root/'Sc6ReplayHost.hpp').read_text()
    declarations=[]
    for name in ('ParticleCopyAction', 'InteriorPhase', 'PauseBoundary', 'CapturePhase',
                 'CaptureAction', 'RestoreOperationPhase', 'SeekPhase', 'SeekAction',
                 'SeekReleaseResult', 'TickAdvancePhase'):
        start=public.index(f'    enum class {name} :')
        declarations.append(public[start:public.index(';',start)+1])
    for name in ('CaptureWitness', 'SeekWitness', 'TickAdvanceWitness'):
        start=public.index(f'    struct {name} {{')
        declarations.append(public[start:public.index('    };',start)+6])
    (tmp_path/'vfx_material_capture_types.inl').write_text('\n'.join(declarations))
    checkpoint=(native_root/'Sc6ReplayHost.Checkpoint.inl').read_text()
    start=checkpoint.index('std::unique_ptr<Sc6ReplayParticleCopy>& Sc6ReplayHost::CaptureParticleCopyOwner()')
    (tmp_path/'vfx_material_capture_driver.inl').write_text(checkpoint[start:])
    start=checkpoint.index('bool Sc6ReplayHost::CaptureOperation(')
    switch=checkpoint.index('    switch(action) {',start)
    cancel=checkpoint.index('    case CaptureAction::Cancel:',switch)
    take=checkpoint.index('    case CaptureAction::Take:',cancel)
    release=checkpoint.index('    case CaptureAction::Release:',take)
    end=checkpoint.index('\n}\n',release)+3
    (tmp_path/'vfx_material_capture_actions.inl').write_text(
        checkpoint[start:switch]+'    switch(action) {\n'+checkpoint[cancel:take]+checkpoint[release:end])
    start=surface.index('bool Sc6ReplayHost::ParticleCopyExperiment(')
    begin=surface.index('    if (action == ParticleCopyAction::Begin ||',start)
    tail=surface.index('    self->particle_command_corrected_=corrected;',begin)
    end=surface.index('\n}\n',tail)+3
    (tmp_path/'vfx_material_capture_queue.inl').write_text(
        surface[start:begin]+
        '    Require(action==ParticleCopyAction::Finish || action==ParticleCopyAction::Poll\n'
        '        || action==ParticleCopyAction::RetireInFlight);\n'
        '    if (!copy) return false;\n'+surface[tail:end])
    start=surface.index('void Sc6ReplayHost::ExecuteParticleCopyCommand(')
    switch=surface.index('    switch (self->particle_copy_action_)',start)
    poll=surface.index('    case ParticleCopyAction::Poll:',switch)
    seal=surface.index('    case ParticleCopyAction::SealCapture:',poll)
    finish=surface.index('    case ParticleCopyAction::Finish:',seal)
    end_finish=surface.index('    // State installation/commit',finish)
    epilogue=surface.index('    if (self->particle_copy_action_ != ParticleCopyAction::Poll)',end_finish)
    end=surface.index('\n}',epilogue)+2
    (tmp_path/'vfx_material_capture_command.inl').write_text(
        surface[start:switch]+'    switch (self->particle_copy_action_) {\n'+surface[poll:seal]+
        surface[finish:end_finish]+'    default: std::abort();\n    }\n'+surface[epilogue:end])
    start=driver.index('void Sc6ReplayHost::AdvanceHistoricalRestore()')
    end=driver.index('    if (particle_command_failed_.load() && operation.phase == RestoreOperationPhase::Committing)',start)
    (tmp_path/'vfx_material_capture_restore.inl').write_text(driver[start:end]+'    std::abort();\n}\n')
    seek=(native_root/'Sc6ReplayHost.Seek.inl').read_text()
    start=seek.index('void Sc6ReplayHost::AdvanceSeek()')
    end=seek.index('    RestoreOperationWitness restore{};',start)
    (tmp_path/'vfx_material_capture_seek.inl').write_text(seek[start:end]+'    std::abort();\n}\n')
    interior=(native_root/'Sc6ReplayHost.Interior.inl').read_text()
    start=interior.index('    AdvanceCaptureOperation();')
    end=interior.index('    auto& timeline_ui=',start)
    (tmp_path/'vfx_material_capture_service.inl').write_text(interior[start:end])
    exports=(module.ROOT/'HorseMod/dllmain.cpp').read_text()
    start=exports.index('    HORSE_MOD_API bool horsemod_start_vfx_completion_observation(')
    end=exports.index('    HORSE_MOD_API bool horsemod_start_niagara_observation(',start)
    exports=exports[start:end]
    (tmp_path/'vfx_observation_exports.inl').write_text(exports)
    names=re.findall(r'HORSE_MOD_API (?:bool|void|std::uint64_t) (horsemod_\w+)\(',exports)
    (tmp_path/'vfx_observation_resolver.inl').write_text('\n'.join(
        f'if(!std::strcmp(name,"{name}"))return reinterpret_cast<T>(&{name});' for name in names))
    (tmp_path/'Unreal').mkdir()
    (tmp_path/'Unreal/UObject.hpp').write_text('#pragma once\n')
    (tmp_path/'Unreal/UObjectArray.hpp').write_text('''#pragma once
namespace RC::Unreal {
struct Item {void* object{};int serial{};void* GetUObject(){return object;}
bool IsValid(bool){return object!=nullptr;}int GetSerialNumber(){return serial;}};
struct FUObjectArray {inline static Item items[320];
static Item* IndexToObject(int i){return i>=0&&i<320?&items[i]:nullptr;}};
}
''')
    (tmp_path/'DynamicOutput').mkdir()
    (tmp_path/'DynamicOutput/DynamicOutput.hpp').write_text('''#pragma once
#include <cstdio>
#include <format>
#ifndef STR
#define STR(x) x
#endif
namespace RC {
using CharType=char;
enum class LogLevel {Default,Warning};
struct Output {template<LogLevel,class... T>static void send(const char* text,T... args) {
    const auto line=std::vformat(text,std::make_format_args(args...));
    std::fwrite(line.data(),1,line.size(),stdout);
}};
}
''')
    (tmp_path/'polyhook2/Detour').mkdir(parents=True)
    (tmp_path/'polyhook2/Detour/x64Detour.hpp').write_text('''#pragma once
namespace PLH {class x64Detour {char retained_size_reserve[1024];
std::uint64_t target_,entry_;std::uint64_t* original_;
protected: std::uint64_t m_fnAddress{};unsigned m_hookSize{},m_chosen_scheme{1}; public:
struct Disassembler {template<class T>std::uint64_t disassemble(std::uint64_t p,std::uint64_t,std::uint64_t,T&){return p;}} m_disasm;
static std::optional<std::uint64_t> calcNearestSz(std::uint64_t p,std::uint64_t,std::uint64_t& n){n=p-image==0x8d1c00?8:(p-image==0x1dc8220||p-image==0x8c8f40)?10:11;return p;}
static bool expandProlSelfJmps(std::uint64_t&,std::uint64_t,std::uint64_t&,std::uint64_t&){return true;}
x64Detour(std::uint64_t target,std::uint64_t entry,std::uint64_t* original)
:target_(target),entry_(entry),original_(original),m_fnAddress(target){}
virtual ~x64Detour()=default;
enum {VALLOC2=1};void setDetourScheme(int value){if(value!=VALLOC2)std::abort();}
bool hook(){if(!ProbeHook(target_))return false;*original_=ProbeInstall(target_,entry_);
m_hookSize=target_-image==0x8d1c00?8:(target_-image==0x1dc8220||target_-image==0x8c8f40)?10:11;
if(target_-image==0x1f05150 && std::strstr(mode,"slots-row-scheme"))m_chosen_scheme=2;
if(target_-image==0x1f05150 && std::strstr(mode,"slots-row-patch"))m_hookSize=18;
ProbePublished(target_);return true;}};}
''')
    fixture=module.ROOT/'tools/replay_vfx_completion_observation_selftest.cpp'
    include=module.ROOT/'HorseMod/horselib/deterministic'
    generated=module.BUILD/'HorseMod/generated'
    batch=tmp_path/'compile.cmd'
    extra = ''
    if getattr(request, 'param', None) == 'real-material':
        # Reuse the existing checkout's dependency and built libraries. No
        # project build or alternate harness: only the optional targets
        # use the actual x64Detour, in the same production-boundary fixture.
        cache = (module.BUILD/'CMakeCache.txt').read_text()
        poly = Path(re.search(r'^PolyHook_2_SOURCE_DIR:STATIC=(.+)$', cache, re.M)[1].strip())
        deps = poly.parent
        libraries = module.BUILD/'LessEqual421__Shipping__Win64/lib'
        includes = [poly, poly/'asmjit/src', deps/'zydis-src/include', deps/'zydis-build',
                    deps/'zydis-src/dependencies/zycore/include', deps/'zydis-build/zycore']
        extra = ' /MD /O2 /DASMJIT_STATIC /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /DHORSE_REAL_MATERIAL_DETOUR'
        extra += ''.join(f' /I"{p}"' for p in includes)
        extra += ''.join(f' "{libraries/(name+".lib")}"' for name in ('PolyHook_2','Zydis','Zycore','asmjit','asmtk'))
        (tmp_path/'polyhook2/Detour/x64Detour.hpp').write_text(f'''#pragma once
#include "{(poly/'polyhook2/Detour/x64Detour.hpp').as_posix()}"
namespace FixturePLH {{class x64Detour : public PLH::x64Detour {{
std::uint64_t target_,entry_;std::uint64_t* original_; public:
x64Detour(std::uint64_t target,std::uint64_t entry,std::uint64_t* original)
:PLH::x64Detour(target,entry,original),target_(target),entry_(entry),original_(original){{}}
bool hook() override {{
 if(!ProbeHook(target_))return false;
 const auto native=ProbeInstall(target_,entry_);
 if(target_-image!=0x1dc8220 && target_-image!=0x1f1e420 && target_-image!=0x1f05150 && target_-image!=0x8d1c00 && target_-image!=0x8c8f40){{*original_=native;ProbePublished(target_);return true;}}
 ProbeMaterialCode(target_,native);
 const bool ok=PLH::x64Detour::hook();
 if(ok && target_-image==0x1f05150)for(const auto& instruction:m_originalInsts)
     if(instruction.hasDisplacement())std::abort(); // RIP-relative CMP must remain native.
 if(ok){{entries[target_-image==0x8d1c00?32:target_-image==0x8c8f40?33:target_-image==0x1dc8220?29:target_-image==0x1f1e420?30:31]=target_;ProbePublished(target_);}}
 std::printf("actual x64Detour rva=%llx installed=%u patch_bytes=%u\\n",target_-image,unsigned(ok),m_hookSize);
 return ok;
}}
}};}}
#define PLH FixturePLH
''')
    batch.write_text(f'@echo off\ncall "{VCVARS}" >nul\ncl /nologo /std:c++20 /EHsc /I. /I"{include}" /I"{generated}"{extra} "{fixture}" /Fe:vfx-observation.exe /Fo:vfx-observation.obj\n')
    built=run_compile(['cmd','/d','/c',str(batch)],cwd=tmp_path,capture_output=True,text=True,timeout=60)
    assert built.returncode==0,built.stdout+built.stderr
    return tmp_path/'vfx-observation.exe'


@pytest.mark.native_contract
@pytest.mark.parametrize('vfx_observer_executable', ['stub', 'real-material'], indirect=True)
@pytest.mark.parametrize('case', ['normal', 'null-pair', 'no-wrapper', 'no-factory', 'wrong-thread',
                                  'reentry', 'wrapper-reentry', 'start-reentry', 'factory-caller',
                                  'wrapper-caller', 'overflow', 'signature', 'install', 'disabled', 'budget'])
def test_trace_creation_selected_native_contract(tmp_path, vfx_observer_executable, case):
    config = tmp_path/'HorseMod/Qualification/replay_diagnostics.ini'
    config.parent.mkdir(parents=True)
    config.write_text('stack_depth=0\nmask='+('0' if case == 'disabled' else '4')+
                      '\nfirst_tick=172\nlast_tick=172\nbyte_limit=73728\n')
    child = subprocess.run([str(vfx_observer_executable), 'trace-diagnostic-creation-'+case],
                           cwd=tmp_path, env={**os.environ, 'LOCALAPPDATA': str(tmp_path)},
                           capture_output=True, text=True, timeout=30)
    (tmp_path/'trace-creation-native.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    assert child.returncode == 0, child.stdout+child.stderr
    assert 'read_only=true last_error_preserved=true' in child.stdout
    lines = [line for line in child.stdout.splitlines() if '[HorseMod] trace creation' in line]
    if case == 'disabled':
        assert not lines
    elif case == 'budget':
        assert len(lines) == 1 and 'reason=diagnostic_byte_limit' in lines[0]
    else:
        assert len(lines) <= 10 and sum(len(line.encode('utf-8'))+2 for line in lines) < 8192
        assert any('birth_journal_complete=false ownership_proven=false qualification=false' in line for line in lines)
        if case in ('normal', 'null-pair'):
            assert any('family=construct' in line and 'identity_tier=PROVISIONAL' in line for line in lines)
            pair = next(line for line in lines if 'family=factory' in line)
            assert 'pair_stable=true' in pair
            assert ('state=0 controller=0 ' if case == 'null-pair' else 'state=12345 controller=6789a ') in pair
    if 'actual x64Detour' in child.stdout and case not in ('signature', 'disabled'):
        assert 'actual x64Detour rva=8d1c00 installed=1 patch_bytes=8' in child.stdout
        if case != 'install':
            assert 'actual x64Detour rva=8c8f40 installed=1 patch_bytes=10' in child.stdout
    print(child.stdout)


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['false-only', 'false-then-true', 'true', 'null',
                                  'unreadable', 'unreadable-kind', 'torn-flag', 'torn-parts', 'torn-kind',
                                  'exit-changed', 'exit-unreadable'])
def test_trace_start_diagnostic_request_selection(tmp_path, vfx_observer_executable, case):
    """Real owned wrapper/Observe; mock Start proves forwarding, not native births.

    Skipped requests must leave both selection and the exact event budget free.
    Exit logs must use entry scalars even if the request changes or is revoked.
    """
    child = subprocess.run([str(vfx_observer_executable), 'trace-diagnostic-request-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    (tmp_path/'trace-start-request.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    assert child.returncode == 0, child.stdout+child.stderr
    lines = child.stdout.splitlines()
    summary, = [line for line in lines if line.startswith('trace request fixture complete ')]
    counts = dict(re.findall(r'(\w+)=([^ ]+)', summary))
    skipped = case not in ('true', 'exit-changed', 'exit-unreadable')
    selected = case != 'false-only'
    assert counts['native_calls'] == counts['entry_callbacks'] == str(2+int(skipped and selected))
    assert counts['selected'] == counts['exit_callbacks'] == str(int(selected))
    assert counts['exit_request_reads'] == '0'
    assert len(child.stdout.encode('utf-8')) < 65536
    records = [dict(re.findall(r'(\w+)=([^ ]+)', line)) for line in lines
               if line.startswith('[HorseMod] trace Start diagnostic event=')]
    if not selected:
        assert not any('trace Start ' in line for line in lines)
        return
    assert [row['phase'] for row in records] == ['entry', 'exit']
    for row in records:
        assert row['create_branch'] == 'true'
        assert row['packed_life_flag14'] == '1'
        assert row['parts_id'] == '7' and row['kind_id'] == '13'
        assert row['request_source'] == 'entry_copy' and row['request_double_read'] == 'true'
        assert row['read_only'] == 'true' and row['qualification'] == 'false'
        assert row['start_returns_void'] == 'true' and row['birth_journal_complete'] == 'false'


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['existing', 'duplicates', 'nulls', 'no-new', 'ref-limit',
                                  'entry-header', 'exit-header', 'entry-overflow', 'exit-overflow',
                                  'detail-overflow', 'component-generation', 'attachment-generation', 'removed-generation',
                                  'zero-serial', 'unstable-ref', 'unstable-header', 'unreadable-data',
                                  'budget', 'removed', 'zero-manager', 'zero-old', 'zero-new',
                                  'zero-component', 'zero-removed', 'zero-ref-limit', 'zero-detail-overflow',
                                  'zero-reused-index', 'zero-reused-pointer', 'zero-manager-reused-index',
                                  'zero-aba-unobserved', 'zero-unstable-vtable', 'zero-unstable-index',
                                  'zero-late-ref', 'zero-owner-header', 'removed-overflow',
                                  'zero-removed-overflow', 'zero-promoted-serial', 'zero-removed-stale'])
def test_trace_start_diagnostic_retained_delta(tmp_path, vfx_observer_executable, case):
    """Real Observe/Emit, RPM and indexed lookup; no fixture census or delta predicate.

    Lazy zero serials must permit provisional retained candidates, with no
    generation/qualification claim. Observable reuse and torn snapshots close
    the comparison. ABA with identical endpoints is explicitly unprovable.
    """
    child = subprocess.run([str(vfx_observer_executable), 'trace-diagnostic-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=30)
    (tmp_path/'trace-start-diagnostic.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    assert child.returncode == 0, child.stdout+child.stderr
    assert 'trace diagnostic fixture complete' in child.stdout
    assert len(child.stdout.encode('utf-8')) < 65536
    if case == 'budget':
        assert 'reason=diagnostic_byte_limit required_bytes=65536' in child.stdout
        assert 'phase=entry' not in child.stdout and 'phase=exit' not in child.stdout
        return
    lines = child.stdout.splitlines()
    details = [line for line in lines if line.startswith('[HorseMod] trace Start attachment ')]
    assert not any('phase=entry ' in line for line in details), 'existing attachments consumed the detail budget'
    def record(prefix, phase):
        rows = [line for line in lines if line.startswith('[HorseMod] trace Start '+prefix+' ')
                and 'phase='+phase+' ' in line]
        assert len(rows) == 1, rows
        return dict(re.findall(r'(\w+)=([^ ]+)', rows[0]))
    entry = record('census', 'entry')
    after = record('census', 'exit')
    assert all('phase=exit ' in line and 'index=4 ' not in line for line in details)
    assert 'birth_journal_complete=false' in child.stdout
    assert after['census_factory_null_births_unobserved'] == 'true'
    assert after['census_returned_pair_observed'] == 'false'  # Start is void.
    assert after['zero_serial_aba_excluded'] == 'false'
    assert entry['qualification'] == after['qualification'] == 'false'
    assert all('qualification=false' in line for line in details)
    assert sum('trace Start diagnostic end ' in line for line in lines) == 2
    provisional = case.startswith('zero-')
    good = ('existing', 'duplicates', 'nulls', 'no-new', 'ref-limit', 'removed',
            'zero-serial', 'zero-manager', 'zero-old', 'zero-new', 'zero-component',
            'zero-removed', 'zero-ref-limit', 'zero-aba-unobserved')
    if case in good:
        old = 254 if case in ('ref-limit', 'zero-ref-limit') else 36
        removed = int(case in ('removed', 'zero-removed'))
        assert entry['identity_coverage_complete'] == after['identity_coverage_complete'] == 'true'
        assert entry['old_attachment_keys'] == str(old)
        assert after['old_attachment_keys'] == str(old-removed)
        assert after['removed_attachment_keys'] == str(removed)
        assert entry['new_attachment_keys'] == '0' and entry['attachment_details'] == '0'
        expected_new = 0 if case == 'no-new' else 1
        assert after['new_attachment_keys'] == str(expected_new)
        assert after['comparison_complete'] == after['details_complete'] == 'true'
        assert after['traces_read'] == 'true'
        assert after['observation_tier'] == ('PROVISIONAL' if provisional else 'EXACT')
        assert after['generation_unproven'] == str(provisional).lower()
        zero_keys = 0 if not provisional or case == 'zero-manager' else 1 if case == 'zero-new' else old-removed
        if provisional and case not in ('zero-manager', 'zero-new', 'zero-old'):
            zero_keys += expected_new
        assert after['provisional_snapshot_keys'] == str(zero_keys)
        assert after['exact_generation_keys'] == str(old-removed+expected_new-zero_keys)
        assert len(details) == expected_new
        if details:
            assert f'index={old+4} ' in details[0] and 'fields_read=true' in details[0]
            row = dict(re.findall(r'(\w+)=([^ ]+)', details[0]))
            assert row['generation_unproven'] == str(provisional).lower()
            assert row['observation_tier'] == after['observation_tier']
            assert row['indexed_manager_match'] == 'true'
            assert row['exact_manager_owner'] == str(not provisional or case in ('zero-old', 'zero-new')).lower()
            zero_new = provisional and case not in ('zero-manager', 'zero-old')
            assert row['identity_tier'] == ('PROVISIONAL' if zero_new else 'EXACT')
            membership = [line for line in lines if line.startswith('[HorseMod] trace Start membership ')]
            assert len(membership) == 4
            assert any('offset=2c0 ' in line and f'count={old+1} matches=1' in line for line in membership)
            assert all('generation_unproven='+str(provisional).lower() in line
                       and 'qualification=false' in line for line in membership)
        removed_rows = [line for line in lines if line.startswith('[HorseMod] trace Start removed ')]
        assert len(removed_rows) == removed
        if removed:
            assert 'index=4 ' in removed_rows[0] and 'logical_destruction_proven=false' in removed_rows[0]
            assert 'observation_tier='+after['observation_tier'] in removed_rows[0]
        if case == 'zero-serial':
            owner = record('owner', 'exit')
            assert owner['component_serial'] == '8104' and owner['manager_serial'] == '0'
            assert 'serial=0 ' in details[0]
        if case == 'duplicates':
            assert after['duplicate_attachment_refs'] == '2'
        if case == 'nulls':
            assert after['null_pairs'] == after['null_attachment_states'] == '1'
    else:
        assert after['traces_read'] == 'false'
        if case in ('detail-overflow', 'zero-detail-overflow'):
            assert after['detail_overflow'] == 'true' and after['new_attachment_keys'] == '17'
            assert after['identity_coverage_complete'] == 'true' and len(details) == 16
            assert after['observation_tier'] == ('PROVISIONAL' if provisional else 'EXACT')
        elif case in ('removed-overflow', 'zero-removed-overflow'):
            assert after['removed_detail_overflow'] == 'true' and after['removed_attachment_keys'] == '17'
            assert after['identity_coverage_complete'] == after['comparison_complete'] == 'true'
            assert after['details_complete'] == 'false' and len(details) == 1
            assert after['observation_tier'] == ('PROVISIONAL' if provisional else 'EXACT')
            assert len([line for line in lines if line.startswith('[HorseMod] trace Start removed ')]) == 16
        else:
            assert not details
        if case in ('entry-overflow', 'exit-overflow'):
            assert (entry if case.startswith('entry') else after)['ref_overflow'] == 'true'
        if case in ('entry-header', 'exit-header'):
            assert (entry if case.startswith('entry') else after)['invalid_header'] == 'true'
        unstable = case in ('component-generation', 'attachment-generation', 'removed-generation',
                            'unstable-ref', 'unstable-header') or (provisional and case not in
                            ('zero-detail-overflow', 'zero-removed-overflow'))
        if unstable:
            assert after['instability'] == 'true'
            assert after['comparison_complete'] == 'false' and after['observation_tier'] == 'UNAVAILABLE'
            assert after['new_attachment_keys'] == after['removed_attachment_keys'] == '0'
        if case.startswith('entry-'):
            assert after['baseline_complete'] == 'false' and after['comparison_complete'] == 'false'


@pytest.mark.native_contract
@pytest.mark.parametrize('entry', ['request', 'prepare'])
@pytest.mark.parametrize('case', ['active', 'worker', 'after-unlock', 'duplicate-retained',
                                  'target-tails', 'recovery', 'failed', 'teardown'])
def test_trace_start_veto_precedes_native_effects(tmp_path, vfx_observer_executable, case, entry):
    """Terminal containment only; neither positive admission nor B recovery.

    The production operation acquisition and existing Start wrapper run against
    an external native entry with exact RCX/RDX arguments. Its first effect is
    an independently observed sentinel. No fast-fail substitute or fixture
    permission participates. On prior source this returns normally and writes
    the sentinel, so the regression fails on behavior, not compilation.
    """
    child = subprocess.run([str(vfx_observer_executable), 'material-settlement-start-veto-'+case, entry],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    detail = (case, child.returncode, child.stdout, child.stderr)
    assert 'trace Start veto exercise begins historical_supported=0' in child.stdout, detail
    assert 'trace Start native mutation' not in child.stdout, detail
    assert child.returncode & 0xffffffff == 0xc0000409, detail


@pytest.mark.native_contract
@pytest.mark.parametrize('entry', ['request', 'prepare'])
@pytest.mark.parametrize('case', ['released', 'preexisting', 'duplicate', 'reentry', 'coverage-lost'])
def test_trace_start_veto_acquisition_and_forwarding(tmp_path, vfx_observer_executable, case, entry):
    child = subprocess.run([str(vfx_observer_executable), 'material-settlement-start-veto-'+case, entry],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    detail = (case, child.returncode, child.stdout, child.stderr)
    assert child.returncode == 0, detail
    assert f'trace Start veto control PASS case={case} historical_supported=0' in child.stdout, detail
    assert 'trace Start native mutation' in child.stdout, detail


@pytest.mark.native_contract
@pytest.mark.parametrize('entry', ['request', 'prepare'])
def test_material_builder_between_clear_and_native_retirement(tmp_path, vfx_observer_executable, entry):
    """Real admission rejects before ownership; idle counters are not a lease.

    Request is extracted in full; direct Prepare includes every check through
    the material boundary, with an observed stop before unmodelled CPU work.
    On old production both entries cross admission, then the original real
    Inspect-unlock -> builder -> native retirement race remains observable.
    Rejection still forwards that builder and callback through production.
    """
    mode='transition-race'+('-prepare' if entry=='prepare' else '')
    child=subprocess.run([str(vfx_observer_executable), 'material-settlement-'+mode],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=(child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    sample=re.search(r'material transition race injected=(\d+) builder_forwarded=(\d+) '
                     r'pending_at_mutation=(\d+) destructive_calls=(\d+) admission_rejected=(\d+) '
                     r'B_retained=(\d+) commit_decided=(\d+) transaction_created=(\d+)',child.stdout)
    assert sample,detail
    injected,forwarded,pending,mutations,rejected,retained,committed,created=map(int,sample.groups())
    assert injected==1 and forwarded==1,detail
    assert retained==1 and committed==0,detail
    assert mutations==0, (
        'P1: native C-only retirement ran after Clear while an intervening material builder was pending',detail)
    assert pending==0 and rejected==1 and created==0,detail


@pytest.mark.native_contract
def test_selected_trace_start_and_mid_helper_observed_before_effects(tmp_path, vfx_observer_executable):
    """Native-shaped Start/helper reach real startup wrappers before writes/borrow."""
    child=subprocess.run([str(vfx_observer_executable),'material-settlement-trace-producer-route'],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=(child.returncode,child.stdout,child.stderr)
    print(child.stdout+child.stderr)
    assert child.returncode==0,detail
    rows=re.findall(r'trace producer stage=(\w+) state=(\d+) builders=(\d+) tasks=(\d+) '
                    r'late_admitted=(\d+) command_allowed=(\d+)',child.stdout)
    assert [r[0] for r in rows]==['start_before_write','helper_before_lookup','helper_borrowed',
                                 'start_after_helper','returned'],detail
    for stage,*values in rows:
        state,builders,tasks,late,command=map(int,values)
        assert (builders,tasks,command)==(0,0,0),('unobserved producer can cross the production command predicate',detail)
        assert (state,late)==((2,1) if stage=='returned' else (1,0)),detail
    assert ('trace producer forwarding starts=1 helpers=1 writes=1 lookups=1 reads_after_return=0 '
            'retirements=0 B_retained=1 committed=0 failed=1 historical_supported=0') in child.stdout,detail
    assert ('trace counts stage=start_before_write starts=1 helpers=0 start_returns=0 helper_returns=0 '
            'active_starts=1 active_helpers=0 threads=1 depth=1 peak=1 reentries=0 concurrent=0 orphan=0') in child.stdout,detail
    assert ('trace counts stage=helper_borrowed starts=1 helpers=1 start_returns=0 helper_returns=0 '
            'active_starts=1 active_helpers=1 threads=1 depth=2 peak=2 reentries=0 concurrent=0 orphan=0') in child.stdout,detail
    assert ('trace counts stage=start_after_helper starts=1 helpers=1 start_returns=0 helper_returns=1 '
            'active_starts=1 active_helpers=0 threads=1 depth=1 peak=2 reentries=0 concurrent=0 orphan=0') in child.stdout,detail
    assert ('trace counts stage=returned starts=1 helpers=1 start_returns=1 helper_returns=1 '
            'active_starts=0 active_helpers=0 threads=0 depth=0 peak=2 reentries=0 concurrent=0 orphan=0') in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['reentry','concurrent','late-snapshot','orphan','capacity','observer-closed','material'])
def test_selected_trace_producer_observation_is_not_a_lease(tmp_path, vfx_observer_executable, case):
    child=subprocess.run([str(vfx_observer_executable),'material-settlement-trace-producer-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=15)
    detail=(child.returncode,child.stdout,child.stderr)
    print(child.stdout+child.stderr)
    assert child.returncode==0,detail
    starts,helpers={'reentry':(2,2),'concurrent':(2,2),'orphan':(0,1),'capacity':(130,0)}.get(case,(1,1))
    assert (f'trace producer forwarding starts={starts} helpers={helpers} writes={starts} lookups={helpers} reads_after_return=0 '
            'retirements=0 B_retained=1 committed=0 failed=1 historical_supported=0') in child.stdout,detail
    assert (f'trace counts stage=returned starts={starts} helpers={helpers} start_returns={starts} helper_returns={helpers} '
            'active_starts=0 active_helpers=0 threads=0 depth=0') in child.stdout,detail
    if case=='reentry':
        assert 'active_starts=2 active_helpers=2 threads=1 depth=4 peak=4 reentries=2 concurrent=0 orphan=0' in child.stdout,detail
    elif case=='concurrent':
        row=re.search(r'trace counts stage=concurrent_held (.+)',child.stdout)
        assert row and 'active_starts=2 active_helpers=2 threads=2 depth=0 peak=2 reentries=0' in row[1],detail
        assert int(re.search(r'concurrent=(\d+)',row[1])[1])>=2,detail
        assert 'trace late receipt admitted=0 returned_state=3' in child.stdout,detail
    elif case=='late-snapshot':
        assert 'trace late snapshot injected=1 sampled_command=1 current_state=2' in child.stdout,detail
    elif case=='material':
        for stage,tasks in [('helper_submitted',2),('returned',2),('vector_returned',1)]:
            assert f'trace producer stage={stage} state=1 builders=0 tasks={tasks} late_admitted=0 command_allowed=0' in child.stdout,detail
        assert 'trace producer stage=callbacks_returned state=2 builders=0 tasks=0 late_admitted=1 command_allowed=0' in child.stdout,detail
        assert 'trace material tasks builders=2 vector_returns=1 refresh_returns=1 GPU_receipt=0' in child.stdout,detail
    if case in ('orphan','capacity'):
        assert 'trace producer stage=returned state=3' in child.stdout,detail
        assert 'trace fresh receipt state=3 C_only_state=3' in child.stdout,detail
    else:
        if case!='material':
            assert 'trace producer stage=returned state=2' in child.stdout,detail
        assert 'trace fresh receipt state=0 C_only_state=4' in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('schedule', ['concurrent', 'same-thread'])
def test_selected_trace_mid_borrow_blocks_internal_c_only_retirement(tmp_path, vfx_observer_executable, schedule):
    """CPU-idle cannot authorize native teardown even below historical admission."""
    mode='material-settlement-borrow-at-retirement'+('-same-thread' if schedule=='same-thread' else '')
    child=subprocess.run([str(vfx_observer_executable), mode],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=(child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    sample=re.search(r'material C-only raw borrow held=(\d+) builders=(\d+) tasks=(\d+) '
                     r'destructive_calls=(\d+) B_retained=(\d+) commit_decided=(\d+) failed=(\d+)',child.stdout)
    assert sample,detail
    held,builders,tasks,mutations,retained,committed,failed=map(int,sample.groups())
    assert (held,builders,tasks,retained,committed)==(1,0,0,1,0),detail
    assert mutations==0,('raw MID borrow crosses the internal native C-only destruction boundary',detail)
    assert failed==1,detail
    assert 'material C-only terminal command_allowed=0 release_allowed=0' in child.stdout,detail
    assert 'material C-only forwarding provider=1 builders=2 callbacks=2' in child.stdout,detail


# Deliberately not collected in ordinary local/G3 runs: preserve the existing
# baseline count, with no xfail or skipped positive-admission case. Opt in with
# HORSE_TRACE_MID_BORROW_RED=1 and select this exact node through replay_test.py.
if os.environ.get('HORSE_TRACE_MID_BORROW_RED') == '1':
    @pytest.mark.native_contract
    def test_selected_trace_mid_borrow_requires_positive_admission(tmp_path, vfx_observer_executable):
        """Bounded RED prerequisite, not a synthetic positive lifetime proof.

        A controlled provider returns a raw MID/proxy before builder entry.
        Request and direct Prepare are production excerpts. Current rejection
        must retain the fixture's B owner and avoid every destructive C-only
        retirement call, including after independently forwarded CPU callbacks.

        Positive acceptance ultimately requires a genuine production exclusion/
        lease and independent vector, refresh, reset/reinitialize, deferred
        release and GPU completion, then an eligible operation that progresses.
        This fixture cannot produce those receipts. Crossing admission below
        is only a necessary prerequisite; even that alone must never turn this
        test green. Extending the real production boundary is required before
        replacing the final explicit fixture-limit failure with positive proof.
        """
        results = {}
        for entry in ('request', 'prepare'):
            directory = tmp_path/entry
            directory.mkdir()
            mode = 'material-settlement-borrow-before-builder'+('-prepare' if entry == 'prepare' else '')
            child = subprocess.run([str(vfx_observer_executable), mode], cwd=directory,
                                   capture_output=True, text=True, timeout=15)
            detail = (entry, child.returncode, child.stdout, child.stderr)
            print(child.stdout + child.stderr)
            assert child.returncode == 0, detail
            rows = {}
            for stage, fields in re.findall(r'^material raw borrow stage=(\w+) (.+)$', child.stdout, re.M):
                assert stage not in rows, detail
                rows[stage] = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', fields)}
            assert list(rows) == ['borrowed', 'submitted', 'vector_returned', 'cpu_returned'], detail
            for stage, held, state, tasks, builders in (
                ('borrowed', 1, 0, 0, 0), ('submitted', 0, 1, 2, 2),
                ('vector_returned', 0, 1, 1, 2), ('cpu_returned', 0, 2, 0, 2),
            ):
                row = rows[stage]
                assert (row['held'], row['state'], row['builders'], row['tasks'], row['builder_calls']) == (
                    held, state, 0, tasks, builders), detail
                assert row['destructive_calls'] == 0, ('C-only retirement crossed a raw MID borrow/completion gap', detail)
                assert row['B_retained'] == 1 and row['commit_decided'] == 0, detail
                if stage != 'cpu_returned':
                    assert row['rejected'] == 1 and row['prefix_crossed'] == 0 and row['transaction_created'] == 0, detail
            assert ('material raw borrow forwarding provider=1 vector_builder=1 refresh_builder=1 '
                    'vector_callback=1 refresh_callback=1') in child.stdout, detail
            assert ('producer_exclusion=unrepresented post_exclusion_borrow=unrepresented '
                    'same_thread_reentry=unrepresented reset_reinitialize=unrepresented '
                    'deferred_release_gpu=unrepresented complete_B_contents=unrepresented') in child.stdout, detail
            results[entry] = rows['cpu_returned']

        # This is the expected current RED, after BOTH entry points' safety and
        # exactly-once checks. It identifies a missing positive architecture,
        # not permission to admit merely because callbacks returned/counters fell.
        assert all(row['prefix_crossed'] == 1 for row in results.values()), (
            'selected trace MID positive admission unavailable: Request/direct Prepare still reject; '
            'implement production borrow exclusion/lease and independent material deferred-release/GPU '
            'completion before expecting supported progress. CPU callback return is not that completion.', results)
        pytest.fail(
            'Admission prefixes alone cannot prove selected MID positive progress. Extend this existing '
            'fixture at genuine production exclusion, post-exclusion borrower/reentry and independent '
            'reset/reinitialize/deferred-release/GPU completion boundaries; Prepare currently stops at '
            'PreparationReached and complete B contents are not modelled. Do not supply a fixture lease.')


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['returned', 'cancel-returned', 'timeout', 'coverage-lost'])
def test_corrected_capture_material_failure_is_terminal(tmp_path, vfx_observer_executable, case):
    """Real host service order, capture queue/Finish and restore/seek decisions.

    Controlled native readback is already complete; it cannot grant a material
    receipt. Both task families enter/return through the production wrappers.
    Eight service turns and a synthetic monotonic deadline bound the test.
    """
    child=subprocess.run([str(vfx_observer_executable), 'material-settlement-corrected-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=(case,child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    sample=re.search(r'corrected material terminal history=(\d+) capture=(\d+) seek=(\d+) '
                     r'finish_queued=(\d+) finish_native=(\d+) owners_retained=(\d+) '
                     r'commit_decided=(\d+) recovered=(\d+) deferred=(\d+) callbacks=(\d+)',child.stdout)
    assert sample,detail
    history,capture,seek,queued,native,retained,committed,recovered,deferred,callbacks=map(int,sample.groups())
    assert native==0 and retained==1 and committed==0 and recovered==0,detail
    assert callbacks==(0 if case in ('timeout','coverage-lost') else 2),detail
    assert (history,capture,seek)==(1,1,1), (
        'P2: corrected capture/Finish must publish terminal material failure to history, capture and seek; retrying Finish is not failure',detail)
    assert queued>=1,('the real corrected Finish command must be exercised',detail)
    assert queued==1 and deferred==1,('a vetoed Finish must not be retried',detail)
    assert 'corrected terminal remains stable=1' in child.stdout,detail
    assert 'corrected terminal cancel_rejected=1 release_rejected=1 late_callbacks_retained=1' in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['particle-pending', 'particle-complete', 'vector-returned'])
def test_material_cpu_tasks_block_host_render_settlement(tmp_path, vfx_observer_executable, case):
    """Particle completion/vector return cannot settle pending material CPU bodies.

    Native callback bodies below are controlled external services with the
    verified task-pointer ABI and recycling boundary, not the game executable.
    No material-complete input reaches production and no B recovery is asserted.
    The existing provider admission guard is neither changed nor exercised here.
    Even both callback returns would not prove deferred-resource/GPU retirement.
    """
    child=subprocess.run([str(vfx_observer_executable), 'material-settlement-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=20)
    detail=(case,child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    sample=re.search(
        r'material CPU boundary particle_complete=(\d+) vector_returns=(\d+) '
        r'refresh_returns=(\d+) pending_bodies=(\d+) render_settled=(\d+)',child.stdout)
    assert sample,detail
    particle,vector,refresh,pending,settled=map(int,sample.groups())
    assert (particle,vector,refresh,pending)=={
        'particle-pending':(0,0,0,2),
        'particle-complete':(1,0,0,2),
        'vector-returned':(1,1,0,1),
    }[case],detail
    assert 'material callbacks forwarded once: vector=1 refresh=1 task_pages_revoked=2' in child.stdout,detail
    assert 'isolated settlement only: admission_unexercised=1 recovery_unexercised=1 downstream_unproved=1' in child.stdout,detail
    assert not settled, (
        'host Render::Settled with pending material CPU task bodies; '
        'particle completion and vector callback return do not complete refresh',detail)
    assert 'material terminal state=2 builders=0 tasks=0 failed_session=1 owners_retained=1 retirements=0' in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['no-material', 'refresh-returned', 'both-returned',
                                  'post-queue', 'reuse', 'active-builder', 'return-value',
                                  'unknown', 'duplicate', 'early-callback', 'overflow', 'timeout',
                                  'observer-disabled', 'observer-closed'])
def test_material_task_guard_controls(tmp_path, vfx_observer_executable, case):
    """Actual builders/callback wrappers and host boundary, not a recovery grant."""
    child=subprocess.run([str(vfx_observer_executable),'material-settlement-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=20)
    detail=(case,child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    assert f'material guard control PASS case={case}' in child.stdout,detail
    assert 'forbidden_return_reads=0' in child.stdout,detail
    if case=='no-material':
        assert 'CPU-idle progress render_settled=1 C_only_rejected=1 retirements=0 B_retained=1' in child.stdout,detail
    else:
        state=3 if case in ('unknown','duplicate','early-callback','overflow') else 2
        assert f'material terminal state={state}' in child.stdout,detail
        assert 'failed_session=1 owners_retained=1 retirements=0' in child.stdout,detail
    if case=='both-returned':
        assert 'particle_complete=1 vector_returns=1 refresh_returns=1 pending_bodies=0 render_settled=0' in child.stdout,detail
    if case=='refresh-returned':
        assert 'particle_complete=1 vector_returns=0 refresh_returns=1 pending_bodies=1 render_settled=0' in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['late','workers','table','partial','identity',
                                  'signature-0','signature-1','signature-2','signature-3',
                                  'signature-4','signature-5','partial-start','partial-helper',
                                  'producer-returned','producer-active'])
def test_material_task_guard_startup_rejects_incomplete_coverage(tmp_path, vfx_observer_executable, case):
    child=subprocess.run([str(vfx_observer_executable),'material-settlement-startup-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=20)
    detail=(case,child.returncode,child.stdout,child.stderr)
    assert child.returncode==0,detail
    pinned={'partial':2,'partial-start':4,'partial-helper':5,'producer-returned':6,'producer-active':6}.get(case,0)
    assert f'material startup rejected pinned_hooks={pinned}' in child.stdout,detail
    if case in ('partial-start','partial-helper','producer-returned','producer-active'):
        starts=int(case!='partial-start')
        helpers=int(case in ('producer-returned','producer-active'))
        assert (f'trace startup coverage=3 starts={starts} helpers={helpers} start_returns={starts} helper_returns={helpers} '
                'active=0 native_starts=1 native_helpers=1 reads_after_return=0') in child.stdout,detail


@pytest.mark.native_contract
@pytest.mark.parametrize('manager_class', ['battle', 'vfx', 'unknown'])
def test_collection18_combat_arm_requires_battle_manager(tmp_path, vfx_observer_executable, manager_class):
    # Native-shaped indexed input and actual bridge/arm/callback code. The
    # fixture's table identities are independent of the production comparison.
    case = 'prearm' if manager_class == 'battle' else 'arm-'+manager_class
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-combat-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    selection = value['collection18_combat_selection']
    assert child.returncode == 0, f'native-shaped combat arm failed: {selection}'
    assert 'manager_table_rva='+{'battle':'327aa20','vfx':'3356f68','unknown':'2000'}[manager_class] in child.stdout
    assert selection['armed'] is (manager_class == 'battle')
    assert selection['invalidated'] is (manager_class != 'battle')
    assert selection['failure'] == (None if manager_class == 'battle' else 'arm_identity_or_phase')
    assert selection['ownership_proven'] is False and selection['completion_proven'] is False
    assert value['owned_bytes'] == 4*1024*1024
    assert 'incoming=0x66c outgoing=0x77c' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    from deterministic_qualification.replay_control import c18_diagnostic_return_marker
    marker = c18_diagnostic_return_marker(child.stdout, 'vfx-observation-fixture')
    if manager_class == 'battle':
        assert selection['prearm_callbacks'] == 1 and marker['arm_frame'] == 170 and marker['selected_frame'] == 177
        assert value['collection18_witness']['material_provider_census']['entry_copy_valid'] is False
        assert value['collection18_witness']['zero_serial_diagnostic']['rows']
        assert 'writer native PASS: executor=2' in child.stdout
    else:
        assert marker is None and value['collection18_witness'] is None
        assert 'bridge_failure=c18_combat_arm_rejected' in child.stdout
        assert 'writer native PASS: executor=1' in child.stdout


@pytest.mark.native_contract
def test_collection18_combat_selection_excludes_setup(tmp_path, vfx_observer_executable):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-combat-prearm'],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0, 'setup callback consumed combat-targeted selection'
    assert 'combat selection pre-arm native return ready=0 forwarded=1' in child.stdout
    from deterministic_qualification.replay_control import c18_diagnostic_return_marker, c18_diagnostic_stop_receipt
    expected = c18_diagnostic_return_marker(child.stdout, 'vfx-observation-fixture')
    assert expected and expected['arm_frame'] == 170 and expected['selected_frame'] == 177
    path = tmp_path/'vfx_completion_observation.json'
    value = json.loads(path.read_bytes())
    selection = value['collection18_combat_selection']
    assert selection['prearm_callbacks'] == 1 and not selection['invalidated']
    assert value['collection18_witness']['material_provider_census']['entry_copy_valid'] is False
    assert value['collection18_witness']['zero_serial_diagnostic']['rows']
    stop = c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', expected)
    assert stop['phase_complete'] is False
    assert not selection['ownership_proven'] and not selection['completion_proven']
    assert 'writer native PASS: executor=2' in child.stdout
    # Reject stale setup association and independently corrupted persisted
    # coordinates. These are reader tests, never fixture-issued native receipts.
    legacy = {k: expected[k] for k in ('entry_sequence','return_sequence','thread','collection','descriptor')}
    with pytest.raises(RuntimeError, match='combat stop marker missing'):
        c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', legacy)
    from deterministic_qualification.replay_control import _validate_c18_combat_marker
    with pytest.raises(RuntimeError, match='coordinates'):
        _validate_c18_combat_marker({})
    import copy
    for key, bad in [('arm_epoch', 699), ('selected_epoch', 706), ('arm_sequence', 99),
                     ('manager', 1234), ('invalidated', True), ('ownership_proven', True)]:
        corrupted = copy.deepcopy(value)
        corrupted['collection18_combat_selection'][key] = bad
        path.write_text(json.dumps(corrupted))
        with pytest.raises(RuntimeError, match='combat sidecar differs'):
            c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', expected)
    path.write_text(json.dumps(value))


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['scene','replay','missing','epoch','late','player','generation','phase','post-scene',
                                  'double-sample','thread'])
def test_collection18_combat_selection_rejects_changed_native_context(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-combat-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0, child.stdout+child.stderr
    assert 'combat selection rejected: forwarded=1' in child.stdout
    assert 'after_return_reads=0' in child.stdout
    assert 'incoming=0x66c outgoing=0x77c' in child.stdout
    from deterministic_qualification.replay_control import c18_diagnostic_return_marker
    assert c18_diagnostic_return_marker(child.stdout, 'vfx-observation-fixture') is None
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    selection = value['collection18_combat_selection']
    assert selection['armed'] and selection['invalidated']
    assert selection['failure'] == ('trajectory_failed' if case in ('scene','replay','missing','post-scene')
                                    else 'entry_identity_phase_or_epoch')
    assert (value['collection18_witness'] is not None) == (case == 'post-scene')
    assert value['owned_bytes'] == 4*1024*1024


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['', '-inline', '-hub-pointer', '-hub-serial', '-mid-serial', '-mid-index', '-flags',
                                  '-collection-header', '-header', '-topology', '-unreadable', '-weak',
                                  '-overflow', '-reentry', '-overlap', '-close', '-writer',
                                  '-inline-negative-count', '-inline-zero-capacity', '-inline-multirow',
                                  '-inline-extra-capacity', '-inline-over-capacity', '-inline-empty-capacity',
                                  '-inline-hub-identity', '-inline-entry-serial', '-inline-hub-vtable',
                                  '-inline-dispatcher-vtable', '-inline-header-unreadable', '-inline-recursion',
                                  '-inline-hub-pointer', '-inline-hub-serial', '-inline-collection-header',
                                  '-inline-storage', '-inline-header', '-inline-listener',
                                  '-listener-weak-zero', '-listener-weak-negative', '-listener-index-negative',
                                  '-listener-index-missing', '-listener-index-null', '-listener-stale',
                                  '-listener-object-flags', '-listener-object-unreadable',
                                  '-listener-sample-serial', '-listener-sample-index', '-listener-table'])
def test_collection18_zero_serial_diagnostic(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    assert 'ordinary C18 protocol/startup PASS: executor_and_unknown_fields_rejected=1' in child.stdout
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witness = value['collection18_witness']
    assert witness['hub']['serial'] == 0 and witness['header_valid'] is False
    census = witness['material_provider_census']
    assert census['entry_copy_valid'] is False and census['slots'] == []
    assert census['memory_shape']['rows'] == []
    assert census['owned_bytes'] <= 256*1024
    diagnostic = witness.get('zero_serial_diagnostic')
    assert isinstance(diagnostic, dict), 'missing separate zero-serial production diagnostic receipt'
    sampled = case in ('', '-inline')
    failure = {'-hub-pointer':'collection_changed', '-hub-serial':'indexed_sample_changed',
               '-mid-serial':'indexed_sample_changed', '-mid-index':'material_identity', '-flags':'indexed_sample_changed',
               '-collection-header':'collection_changed', '-header':'mid_shape_entry_changed',
               '-topology':'entry_changed', '-unreadable':'mid_shape_unreadable', '-weak':'listener_weak_serial_nonpositive',
               '-overflow':'detail_capacity', '-reentry':'occurrence_invalidated', '-overlap':'occurrence_invalidated',
               '-close':'occurrence_invalidated', '-writer':'occurrence_invalidated'}.get(case, '')
    failure = {'-inline-negative-count':'entry_header_shape', '-inline-zero-capacity':'entry_header_shape',
               '-inline-multirow':'entry_header_shape', '-inline-extra-capacity':'entry_header_shape',
               '-inline-over-capacity':'entry_header_shape', '-inline-empty-capacity':'entry_header_shape',
               '-inline-hub-identity':'entry_hub_identity', '-inline-entry-serial':'entry_hub_identity',
               '-inline-hub-vtable':'entry_hub_vtable', '-inline-dispatcher-vtable':'entry_dispatcher_vtable',
               '-inline-header-unreadable':'entry_header_unreadable', '-inline-recursion':'occurrence_invalidated',
               '-inline-hub-pointer':'collection_changed', '-inline-hub-serial':'indexed_sample_changed',
               '-inline-collection-header':'collection_changed', '-inline-storage':'collection_changed',
               '-inline-header':'mid_shape_entry_changed', '-inline-listener':'entry_changed'}.get(case, failure)
    failure = {'-listener-weak-zero':'listener_weak_serial_nonpositive',
               '-listener-weak-negative':'listener_weak_serial_nonpositive',
               '-listener-index-negative':'listener_indexed_lookup', '-listener-index-missing':'listener_indexed_lookup',
               '-listener-index-null':'listener_indexed_lookup', '-listener-stale':'listener_weak_serial_mismatch',
               '-listener-object-flags':'listener_object_sample', '-listener-object-unreadable':'listener_object_sample',
               '-listener-sample-serial':'listener_sample_identity_mismatch',
               '-listener-sample-index':'listener_sample_identity_mismatch', '-listener-table':'listener_receiver_vtable'}.get(case, failure)
    assert diagnostic['status'] == ('sampled' if sampled else 'rejected'), diagnostic
    assert diagnostic['failure'] == failure, diagnostic
    assert len(diagnostic['rows']) == (2 if case == '-inline' else 4 if sampled else 0)
    assert diagnostic['entry_samples_match'] is sampled
    assert diagnostic['omitted_count'] == (4 if case == '-overflow' else 0 if sampled else None)
    if case.startswith('-listener-'):
        assert 'header_reads=0 world_calls=0' in child.stdout
        assert all(diagnostic[key] is None for key in ('hub', 'collection_header', 'descriptor_fields', 'matched_count'))
    if case == '-inline':
        assert 'c18 native inline fixture: count=1 capacity=1 heap=null listeners=1' in child.stdout
        assert diagnostic['collection_header'] == dict(data=0, count=1, capacity=1)
        assert all(row['listener_row'] == witness['collection'] and row['listener_row_index'] == 0
                   for row in diagnostic['rows'])
    assert diagnostic['generation_unknown'] is True and diagnostic['provider_census_valid'] is False
    assert diagnostic['ownership_permission'] is False and diagnostic['resource_completion_proven'] is False
    assert diagnostic['rollback_admission'] is False and diagnostic['writer_exclusion_proven'] is False
    assert diagnostic['total_retained_native_bytes'] is None
    assert diagnostic['entry_sequence'] == witness['entry_sequence']
    assert diagnostic['thread'] == witness['thread'] and diagnostic['collection'] == witness['collection']
    assert diagnostic['descriptor'] == witness['descriptor']
    assert value['owned_bytes'] == 4*1024*1024
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    path = tmp_path/'vfx_completion_observation.json'
    retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_zero_serial_diagnostic'] == diagnostic
    assert retained['vfx_collection18_material_provider_census']['entry_copy_valid'] is False
    assert retained['vfx_completion_observation_phase_complete'] is False
    from deterministic_qualification.replay_control import c18_diagnostic_stop_receipt, c18_diagnostic_return_marker
    expected = c18_diagnostic_return_marker(child.stdout, 'vfx-observation-fixture')
    assert expected, child.stdout
    stop = c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', expected)
    assert stop['entry_sequence'] == witness['entry_sequence'] and stop['phase_complete'] is False
    assert stop['zero_serial_status'] == diagnostic['status']
    assert Path(stop['raw']['path']).read_bytes() == path.read_bytes()
    with pytest.raises(RuntimeError, match='differs from native stop marker'):
        c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', dict(expected, entry_sequence=expected['entry_sequence']+1))
    for row in diagnostic['rows']:
        assert row['material']['serial'] == 0
        assert row['vector_payload_capacity_bytes'] == 320
        assert row['proxy_occupied'] == [True, True, False]
        assert row['receiver']['serial'] > 0 and 'valid' not in row['material']
    if sampled:
        import copy
        for field, bad in [('provider_census_valid', True), ('generation_unknown', False),
                           ('ownership_permission', True), ('rollback_admission', True),
                           ('entry_sequence', witness['entry_sequence']+1), ('total_retained_native_bytes', 320)]:
            corrupted = copy.deepcopy(value)
            corrupted['collection18_witness']['zero_serial_diagnostic'][field] = bad
            path.write_text(json.dumps(corrupted))
            rejected = {}
            retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, field
            assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
        path.write_text(json.dumps(value))
    if case == '-inline':
        import copy
        # Reader exception is only for the collection's single inline row.
        for field, bad in [('count', 2), ('capacity', 0), ('capacity', 2), ('capacity', 65)]:
            corrupted = copy.deepcopy(value)
            corrupted['collection18_witness']['zero_serial_diagnostic']['collection_header'][field] = bad
            path.write_text(json.dumps(corrupted))
            rejected = {}
            retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, (field, bad)
            assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
        for field, bad in [('listener_row', witness['collection']+0x40), ('listener_row_index', 1),
                           ('vector_array', dict(data=0, count=1, capacity=1))]:
            corrupted = copy.deepcopy(value)
            corrupted['collection18_witness']['zero_serial_diagnostic']['rows'][0][field] = bad
            path.write_text(json.dumps(corrupted))
            with pytest.raises(RuntimeError, match='diagnostic schema'):
                c18_diagnostic_stop_receipt(path, 'vfx-observation-fixture', tmp_path/'objects', expected)
        path.write_text(json.dumps(value))


@pytest.mark.native_contract
@pytest.mark.parametrize('case,count', [('observed', 145), ('limit', 192), ('overflow', 193),
                                      ('proxy', 145), ('cache', 145), ('header', 145), ('serial', 145), ('topology', 145)])
def test_collection18_material_shape_capacity(tmp_path, vfx_observer_executable, case, count):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero-capacity-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0
    assert f'shape capacity native occurrences={count} indexed_mids=2 heap_listeners=1 providers=2' in child.stdout
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    path = tmp_path/'vfx_completion_observation.json'
    value = json.loads(path.read_bytes()); w = value['collection18_witness']; d = w['zero_serial_diagnostic']
    if case == 'observed' and d['failure']:
        assert (d['failure'], d['row_limit'], d['matched_count'], d['omitted_count'], d['rows']) == (
            'detail_capacity', 16, 145, 129, [])
        print('pre-fix capacity rejection:', json.dumps({k: d[k] for k in ('failure','row_limit','matched_count','omitted_count')}))
    changed = case in ('proxy', 'cache', 'header', 'serial', 'topology')
    failure = {'proxy':'mid_shape_entry_changed', 'cache':'mid_shape_entry_changed', 'header':'mid_shape_entry_changed',
               'serial':'indexed_sample_changed', 'topology':'entry_changed', 'overflow':'detail_capacity'}.get(case, '')
    sampled = not failure
    assert d['failure'] == failure, d
    assert d['row_limit'] == 192 and d['version'] == 4
    assert d['matched_count'] == (None if changed else count)
    assert d['omitted_count'] == (None if changed else max(0, count-192))
    assert d['entry_samples_match'] is sampled
    assert d['status'] == ('sampled' if sampled else 'rejected')
    assert len(d['rows']) == (count if sampled else 0)
    assert d['generation_unknown'] is True and d['total_retained_native_bytes'] is None
    for key in ('provider_census_valid', 'ownership_permission', 'resource_completion_proven', 'rollback_admission',
                'writer_exclusion_proven', 'covers_native_start_births'):
        assert d[key] is False
    census = w['material_provider_census']
    assert census['entry_copy_valid'] is False and census['failure'] == 'entry_invalidated'
    assert census['listeners'] == census['providers'] == census['slots'] == census['memory_shape']['rows'] == []
    assert w['header_valid'] is False and census['owned_bytes'] == 243294 and value['owned_bytes'] == 4*1024*1024
    assert path.stat().st_size <= 4096+512*4096+32768-65536  # Rebalanced fixed buffer, no extra copy.
    for index, row in enumerate(d['rows']):
        slots = (count+1)//2; provider = int(index >= slots); slot = index-provider*slots
        secondary = (provider+slot) % 2
        assert (row['provider_index'], row['slot_index'], row['trace_array_offset'], row['trace_ref_index']) == (
            provider, slot, 0x418+provider*0x10, 0)
        assert row['listener_row_index'] == 0 and row['receiver']['index'] == 7
        assert row['trace_actor']['index'] == 15+provider*2 and row['provider']['index'] == 16+provider*2
        assert row['material']['index'] == 20+secondary and row['material']['serial'] == 0
        assert row['vector_array']['count'] == 1+secondary and row['vector_array']['capacity'] == 8+secondary
        assert row['vector_payload_capacity_bytes'] == (8+secondary)*0x28
        assert row['proxy_occupied'] == ([False, False, True] if secondary else [True, True, False])
        assert row['cache_occupied'] == ([bool(i % 2) for i in range(12)] if secondary else [True]+[False]*10+[True])
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}; retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_zero_serial_diagnostic'] == d
    assert retained['vfx_completion_observation_phase_complete'] is False
    from copy import deepcopy
    changes = ['row_limit', 'version', 'omitted']
    if sampled:
        changes += ['duplicate', 'order', 'identity']
    for change in changes:
        bad = deepcopy(value); b = bad['collection18_witness']['zero_serial_diagnostic']
        if change == 'row_limit': b['row_limit'] = 193
        if change == 'version': b['version'] = 3  # Version3's shape limit remains16.
        if change == 'omitted': b['failure'] = 'detail_capacity'; b['omitted_count'] = 0
        if change == 'duplicate': b['rows'][1] = deepcopy(b['rows'][0])
        if change == 'order': b['rows'][0], b['rows'][1] = b['rows'][1], b['rows'][0]
        if change == 'identity': b['rows'][-1]['material']['serial'] += 1
        path.write_text(json.dumps(bad)); rejected = {}
        retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
        assert 'vfx_completion_observation_collection_error' in rejected, change
        assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
    path.write_text(json.dumps(value))


@pytest.mark.workflow
@pytest.mark.parametrize('change', ['', 'version', 'limit', 'omitted'])
def test_collection18_live_shape_capacity_rejection_keeps_legacy_limit(tmp_path, change):
    import hashlib
    import replay_test as module
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    digest = '81d518b53a1f2c8ece8c3291766634e9c7494c32af64d3f1ccb3627ca06e0f10'
    source = module.OUTPUT/'evidence/objects'/f'{digest}.json'
    if not source.is_file():
        pytest.skip('requires immutable measured 2026-09-27 detail_capacity rejection')
    raw = source.read_bytes(); assert hashlib.sha256(raw).hexdigest() == digest
    value = json.loads(raw); d = value['collection18_witness']['zero_serial_diagnostic']
    assert value['run_id'] == 'replay-be3a2e90707142e6bf9d4d77b3e4e4c5'
    assert (d['version'], d['row_limit'], d['failure'], d['matched_count'], d['omitted_count']) == (
        3, 16, 'detail_capacity', 145, 129)
    if change == 'version': d['version'] = 4
    if change == 'limit': d['row_limit'] = 192
    if change == 'omitted': d['omitted_count'] = 0
    path = tmp_path/'vfx.json'; path.write_text(json.dumps(value)); retained = {}
    retain_vfx_completion_observation(retained, path, value['run_id'], tmp_path/'objects')
    if change:
        assert 'vfx_completion_observation_collection_error' in retained
        assert 'vfx_collection18_zero_serial_diagnostic' not in retained
    else:
        assert 'vfx_completion_observation_collection_error' not in retained, retained
        assert retained['vfx_collection18_zero_serial_diagnostic'] == d
        assert d['rows'] == [] and d['provider_census_valid'] is False
    assert retained['vfx_completion_observation_phase_complete'] is False


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['', '-header', '-header-during', '-generation', '-unreadable', '-pointer', '-overflow'])
def test_collection18_selected_material_memory_shape(tmp_path, vfx_observer_executable, case):
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    detail = f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode == 0, detail
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout, detail
    assert 'material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1' in child.stdout, detail
    assert 'return read attempts=0 guarded-read control=1' in child.stdout, detail
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout, detail
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    census = value['collection18_witness']['material_provider_census']
    assert census['entry_copy_valid'] is (case != '-generation'), census
    shape = census.get('memory_shape')
    assert isinstance(shape, dict), 'missing production collection18 material memory-shape receipt'
    failure = {'-header': 'mid_shape_entry_changed', '-header-during': 'vector_header_changed',
               '-generation': 'provider_census_invalid', '-unreadable': 'mid_shape_unreadable',
               '-pointer': 'vector_pointer_shape', '-overflow': 'detail_capacity'}.get(case, '')
    assert shape['version'] == 2 and shape['complete'] is (not case) and shape['failure'] == failure, shape
    assert shape['row_limit'] == 192
    assert shape['omitted_count'] == (None if case == '-generation' else 4 if case == '-overflow' else 0)
    assert shape['matched_count'] == (None if case == '-generation' else 196 if case == '-overflow' else 4)
    assert shape['entry_samples_match'] is (case in ('', '-overflow'))
    assert shape['total_retained_native_bytes'] is None
    assert shape['ownership_permission'] is False and shape['resource_completion_proven'] is False
    assert shape['writer_exclusion_proven'] is False and shape['covers_native_start_births'] is False
    assert value['owned_bytes'] == 4*1024*1024
    assert value['collection18_witness']['returned'] is True
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                    'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_material_provider_census'] == census
    if case not in ('', '-overflow'):
        assert shape['rows'] == []
        return
    assert len(shape['rows']) == (192 if case else 4)  # Ordered occurrences, not owned allocations.
    for row in shape['rows']:
        assert row['material']['index'] == 20 and row['material']['serial'] == 120
        provider = census['providers'][row['provider_index']]
        assert row['provider'] == provider['provider'] and row['trace_actor'] == provider['trace_actor']
        assert row['slot_index'] == (shape['rows'].index(row) % 49 if case else 0)
        assert row['vector_array']['count'] == 1 and row['vector_array']['capacity'] == 8
        assert row['vector_pointer_shape_valid'] is True
        assert row['vector_payload_capacity_bytes'] == 320
        assert row['proxy_occupied'] == [True, True, False]
        assert row['cache_occupied'] == [True]+[False]*10+[True]


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['chara-receiver-handler', 'chara-fighter-handler', 'chara-receiver-unknown',
                                  'type-zero-rva', 'type-below-base', 'type-wide', 'type-null',
                                  'listener-stale', 'listener-object-unreadable', 'type-reentry', 'type-close', 'good'])
def test_collection18_zero_serial_rejected_type_sample(tmp_path, vfx_observer_executable, case):
    suffix = '' if case == 'good' else '-'+case
    child = subprocess.run([str(vfx_observer_executable), 'collection18-material-shape-zero'+suffix],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0
    calls = 2 if case == 'type-reentry' else 1
    # The existing outer-owner message is fixed; executor is the native count.
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout
    assert f'writer native PASS: executor={calls} ' in child.stdout
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout
    assert 'material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    sidecar = tmp_path/'vfx_completion_observation.json'
    original = sidecar.read_bytes()
    value = json.loads(original)
    w = value['collection18_witness']; d = w['zero_serial_diagnostic']
    failure = {'chara-fighter-handler': 'selected_fighter_vtable', 'type-null': 'listener_object_sample',
               'listener-stale': 'listener_weak_serial_mismatch', 'listener-object-unreadable': 'listener_object_sample',
               'type-reentry': 'occurrence_invalidated', 'type-close': 'occurrence_invalidated', 'good': ''}.get(case, 'listener_receiver_vtable')
    assert d['failure'] == failure and w['returned'] is True
    expected = failure in ('listener_receiver_vtable', 'selected_fighter_vtable')
    if expected:
        detail = d.get('rejected_type')
        assert isinstance(detail, dict), 'missing production entry-time rejected-type detail'
        assert detail == dict(scope='single_entry_sample', subject='selected_fighter' if case == 'chara-fighter-handler' else 'listener_receiver',
                              listener_row_index=1, object_index=12 if case == 'chara-fighter-handler' else 8,
                              object_serial=0 if case == 'chara-fighter-handler' else 108,
                              vtable_rva={'type-zero-rva': 0, 'type-below-base': None, 'type-wide': None,
                                          'chara-receiver-unknown': 0x2000}.get(case, 0x326b8d8),
                              image_membership_proven=False)
    else:
        assert 'rejected_type' in d and d['rejected_type'] is None
    assert d['version'] == 4 and d['generation_unknown'] is True
    assert d['provider_census_valid'] is False and d['entry_samples_match'] is (case == 'good')
    if case != 'good':
        assert d['rows'] == [] and all(d[k] is None for k in ('hub', 'collection_header', 'descriptor_fields'))
    for key in ('ownership_permission', 'resource_completion_proven', 'rollback_admission', 'writer_exclusion_proven'):
        assert d[key] is False
    census = w['material_provider_census']
    assert census['entry_copy_valid'] is False and census['failure'] == 'entry_invalidated'
    assert census['listeners'] == census['providers'] == census['slots'] == census['memory_shape']['rows'] == []
    assert d['row_limit'] == 192 and census['owned_bytes'] <= 256*1024 and value['owned_bytes'] == 4*1024*1024
    from deterministic_qualification.replay_control import retain_vfx_completion_observation, c18_diagnostic_return_marker, c18_diagnostic_stop_receipt
    retained = {}
    retain_vfx_completion_observation(retained, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_zero_serial_diagnostic'] == d
    assert retained['vfx_completion_observation_phase_complete'] is False
    marker = c18_diagnostic_return_marker(child.stdout, 'vfx-observation-fixture')
    stop = c18_diagnostic_stop_receipt(sidecar, 'vfx-observation-fixture', tmp_path/'objects', marker)
    assert stop['zero_serial_status'] == d['status'] and stop['phase_complete'] is False
    assert Path(stop['raw']['path']).read_bytes() == original
    if case == 'chara-receiver-handler':
        from copy import deepcopy
        mutations = [(key, bad) for key, bad in (
            ('listener_row_index', -1), ('listener_row_index', 64), ('listener_row_index', True),
            ('object_index', -1), ('object_index', 0x80000000), ('object_serial', 0), ('object_serial', 0x80000000),
            ('vtable_rva', -1), ('vtable_rva', 0x100000000), ('vtable_rva', True), ('vtable_rva', 0x326b2e0),
            ('subject', 'selected_fighter'), ('scope', 'owned'), ('image_membership_proven', True), ('address', 123))]
        for key, bad in mutations:
            corrupt = deepcopy(value); corrupt['collection18_witness']['zero_serial_diagnostic']['rejected_type'][key] = bad
            sidecar.write_text(json.dumps(corrupt)); rejected = dict(retained)
            retain_vfx_completion_observation(rejected, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, (key, bad, rejected)
            assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
        for version, detail in ((1, d['rejected_type']), (2, None), (3, None), (5, d['rejected_type']), (True, None)):
            corrupt = deepcopy(value); cd = corrupt['collection18_witness']['zero_serial_diagnostic']
            cd['version'] = version; cd['rejected_type'] = detail
            sidecar.write_text(json.dumps(corrupt))
            with pytest.raises(RuntimeError, match='diagnostic schema'):
                c18_diagnostic_stop_receipt(sidecar, 'vfx-observation-fixture', tmp_path/'objects', marker)
    # Model the former class policy only for the offline legacy reader: this
    # comparison payload never supplies native capture or admission input.
    legacy = json.loads(original); ld = legacy['collection18_witness']['zero_serial_diagnostic']
    legacy['collection18_material_observation_version'] = 2
    legacy['collection18_witness']['material_provider_census']['version'] = 2
    ld['version'] = 1; ld['row_limit'] = 16; ld.pop('rejected_type')
    for row in ld['rows']:
        row['receiver']['table'] = w['image_base']+0x3268078
    sidecar.write_text(json.dumps(legacy)); retained = {}
    retain_vfx_completion_observation(retained, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert 'rejected_type' not in retained['vfx_collection18_zero_serial_diagnostic']
    sidecar.write_bytes(original)


@pytest.mark.workflow
@pytest.mark.parametrize('changed', [None, 'receiver_chara', 'reinterpret_v3', 'version_only'])
def test_collection18_measured_trace_receiver_legacy_rejection(tmp_path, changed):
    """The actual version2 live rejection stays evidence under its recorded class policy."""
    import hashlib
    import replay_test as module
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    digest = 'c42d6b6ff000cc68dfa15a60d0b8fb986ccb579f44dc05bffa154d8628168179'
    source = module.OUTPUT/'evidence/objects'/f'{digest}.json'
    if not source.is_file():
        pytest.skip('requires immutable measured 2026-09-27 TraceEventHandler rejection')
    raw = source.read_bytes(); assert hashlib.sha256(raw).hexdigest() == digest
    value = json.loads(raw); w = value['collection18_witness']; d = w['zero_serial_diagnostic']
    assert value['run_id'] == 'replay-9d8009d431bf49f4811e21e9dc6863ba'
    assert d['version'] == 2 and d['failure'] == 'listener_receiver_vtable'
    assert d['rejected_type'] == dict(scope='single_entry_sample', subject='listener_receiver', listener_row_index=0,
                                     object_index=310145, object_serial=8261, vtable_rva=0x326b2e0, image_membership_proven=False)
    if changed == 'receiver_chara':
        d['rejected_type']['vtable_rva'] = 0x3268078  # Contradicts the recorded version2 predicate.
    elif changed in ('reinterpret_v3', 'version_only'):
        d['version'] = 3
        if changed == 'reinterpret_v3':
            value['collection18_material_observation_version'] = w['material_provider_census']['version'] = 3
    sidecar = tmp_path/'vfx.json'; sidecar.write_text(json.dumps(value)); retained = {}
    retain_vfx_completion_observation(retained, sidecar, value['run_id'], tmp_path/'objects')
    if changed:
        assert 'vfx_completion_observation_collection_error' in retained, retained
        assert 'vfx_collection18_zero_serial_diagnostic' not in retained
    else:
        assert 'vfx_completion_observation_collection_error' not in retained, retained
        assert retained['vfx_collection18_zero_serial_diagnostic'] == d
        assert d['status'] == 'rejected' and d['rows'] == [] and d['provider_census_valid'] is False
    assert retained['vfx_completion_observation_phase_complete'] is False


@pytest.mark.native_contract
@pytest.mark.parametrize('path', ['ordinary', 'zero_serial'])
@pytest.mark.parametrize('case', ['good', 'receiver-chara', 'receiver-handler', 'receiver-unknown', 'fighter-handler', 'fighter-unknown'])
def test_collection18_trace_receiver_and_chara_fighter_types(tmp_path, vfx_observer_executable, path, case):
    """Native TraceEventHandler receiver and descriptor-selected Chara reach C18Before."""
    zero = path == 'zero_serial'
    mode = 'collection18-material-shape'+('-zero' if zero else '')+'-chara-'+case
    child = subprocess.run([str(vfx_observer_executable), mode],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0
    bad_table = '326b8d8' if case.endswith('-handler') else '3268078' if case.endswith('-chara') else '2000'
    receiver_table = bad_table if case.startswith('receiver-') else '326b2e0'
    fighter_table = bad_table if case.startswith('fighter-') else '3268078'
    assert f'character tables receiver=0x{receiver_table} fighter=0x{fighter_table} distinct_indexed_objects=1' in child.stdout
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout
    assert 'material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1' in child.stdout
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    sidecar = tmp_path/'vfx_completion_observation.json'
    original = sidecar.read_bytes()
    value = json.loads(original)
    witness = value['collection18_witness']
    assert witness['selected'] is True and witness['returned'] is True
    assert witness['header_valid'] is (not zero)
    census = witness['material_provider_census']
    good = case == 'good'
    if zero:
        assert census['entry_copy_valid'] is False and census['failure'] == 'entry_invalidated'
        assert census['listeners'] == census['providers'] == census['slots'] == census['memory_shape']['rows'] == []
        receipt = witness['zero_serial_diagnostic']
        failure = '' if good else 'listener_receiver_vtable' if case.startswith('receiver-') else 'selected_fighter_vtable'
        if good and receipt['failure']:
            # Retain the real pre-fix discriminator, not just a grouped failure.
            assert receipt['rejected_type'] == dict(scope='single_entry_sample', subject='listener_receiver',
                listener_row_index=1, object_index=8, object_serial=108, vtable_rva=0x326b2e0, image_membership_proven=False)
            print('rejected native TraceEventHandler:', json.dumps(receipt['rejected_type']))
        assert receipt['failure'] == failure, receipt
        assert receipt['status'] == ('sampled' if good else 'rejected')
        assert receipt['generation_unknown'] is True and receipt['provider_census_valid'] is False
        assert receipt['rollback_admission'] is False
        bindings, fighter_key = receipt['rows'], 'fighter'
    else:
        assert witness['zero_serial_diagnostic'] is None
        failure = '' if good else 'listener_identity' if case.startswith('receiver-') else 'target_unresolved'
        assert census['failure'] == failure, census
        assert census['entry_copy_valid'] is good
        if not good:
            assert census['listeners'] == census['providers'] == census['slots'] == []
        receipt = census['memory_shape']
        assert receipt['complete'] is good
        bindings, fighter_key = census['listeners'], 'selected_fighter'
    assert receipt['entry_samples_match'] is good
    assert len(receipt['rows']) == (4 if good else 0)
    assert receipt['row_limit'] == 192 and receipt['total_retained_native_bytes'] is None
    for key in ('ownership_permission', 'resource_completion_proven', 'writer_exclusion_proven', 'covers_native_start_births'):
        assert receipt[key] is False
    for binding in bindings:
        receiver, fighter = binding['receiver'], binding[fighter_key]
        assert receiver['table'] == witness['image_base']+0x326b2e0
        assert fighter['table'] == witness['image_base']+0x3268078
        assert receiver['index'] in (7, 8) and receiver['serial'] == 100+receiver['index']
        assert fighter['index'] == 12 and fighter['serial'] == (0 if zero else 112)
        assert receiver['address'] != fighter['address']
        assert binding['battle_manager']['table'] == witness['image_base']+0x327aa20
    assert census['owned_bytes'] <= 256*1024 and value['owned_bytes'] == 4*1024*1024
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_material_provider_census'] == census
    assert retained['vfx_completion_observation_phase_complete'] is False
    if zero:
        assert retained['vfx_collection18_zero_serial_diagnostic'] == receipt
    if good:
        # The reader must also reject consistently mistyped objects, without
        # falling back to same-world or same-getter class equivalence.
        from copy import deepcopy
        for field in ('receiver', fighter_key):
            for table in ((0x3268078, 0x326b8d8, 0x2000) if field == 'receiver' else (0x326b2e0, 0x326b8d8, 0x2000)):
                bad = deepcopy(value)
                w = bad['collection18_witness']
                rows = w['zero_serial_diagnostic']['rows'] if zero else w['material_provider_census']['listeners']
                for row in rows:
                    row[field]['table'] = witness['image_base']+table
                sidecar.write_text(json.dumps(bad))
                rejected = dict(retained)
                retain_vfx_completion_observation(rejected, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
                assert 'vfx_completion_observation_collection_error' in rejected, rejected
                assert 'vfx_collection18_material_provider_census' not in rejected
                assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
                assert rejected['vfx_completion_observation_phase_complete'] is False
        sidecar.write_bytes(original)


@pytest.mark.native_contract
@pytest.mark.parametrize('path', ['ordinary', 'zero_serial'])
@pytest.mark.parametrize('manager_class', ['battle', 'vfx', 'unknown'])
def test_collection18_registry_target_requires_battle_manager(tmp_path, vfx_observer_executable, path, manager_class):
    """Native registry input reaches the real callback/census owner, not an injected receipt."""
    zero = path == 'zero_serial'
    mode = 'collection18-material-shape'+('-zero' if zero else '')+'-registry-'+manager_class
    child = subprocess.run([str(vfx_observer_executable), mode],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    (tmp_path/'c18-fixture.stdout.log').write_text(child.stdout+child.stderr, encoding='utf-8')
    print(child.stdout+child.stderr)
    assert child.returncode == 0
    table = {'battle': '327aa20', 'vfx': '3356f68', 'unknown': '2000'}[manager_class]
    assert f'material registry target table_rva=0x{table} index=11' in child.stdout
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout
    assert 'material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1' in child.stdout
    assert 'material shape LastError PASS: incoming=0x66c outgoing=0x77c resource_reads=0' in child.stdout
    assert 'return read attempts=0 guarded-read control=1' in child.stdout
    value = json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witness = value['collection18_witness']
    assert witness['selected'] is True and witness['returned'] is True
    assert witness['header_valid'] is (not zero)
    census = witness['material_provider_census']
    shape = census['memory_shape']
    expected = manager_class == 'battle'
    failure = '' if expected else 'target_unresolved'
    if zero:
        assert census['entry_copy_valid'] is False and census['failure'] == 'entry_invalidated'
        assert census['listeners'] == census['providers'] == census['slots'] == shape['rows'] == []
        receipt = witness['zero_serial_diagnostic']
        assert receipt['failure'] == failure, receipt
        assert receipt['status'] == ('sampled' if expected else 'rejected')
        assert receipt['generation_unknown'] is True and receipt['provider_census_valid'] is False
        assert receipt['rollback_admission'] is False
        targets = [row['battle_manager'] for row in receipt['rows']]
    else:
        assert witness['zero_serial_diagnostic'] is None
        assert census['failure'] == failure, census
        assert census['entry_copy_valid'] is expected
        if not expected:
            assert census['listeners'] == census['providers'] == census['slots'] == []
        receipt = shape
        assert receipt['complete'] is expected
        targets = [row['battle_manager'] for row in census['listeners']]
    assert receipt['entry_samples_match'] is expected
    assert len(receipt['rows']) == (4 if expected else 0)
    assert receipt['row_limit'] == 192
    assert receipt['ownership_permission'] is False and receipt['resource_completion_proven'] is False
    assert receipt['writer_exclusion_proven'] is False and receipt['covers_native_start_births'] is False
    assert receipt['total_retained_native_bytes'] is None
    for target in targets:
        assert target['index'] == 11 and target['serial'] == (0 if zero else 111)
        assert target['table'] == witness['image_base']+0x327aa20
    for row in receipt['rows']:
        assert row['vector_payload_capacity_bytes'] == 320 and row['proxy_occupied'] == [True, True, False]
    assert census['owned_bytes'] <= 256*1024 and value['owned_bytes'] == 4*1024*1024
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    retained = {}
    retain_vfx_completion_observation(retained, tmp_path/'vfx_completion_observation.json',
                                    'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    assert retained['vfx_collection18_material_provider_census'] == census
    assert retained['vfx_completion_observation_phase_complete'] is False
    if zero:
        assert retained['vfx_collection18_zero_serial_diagnostic'] == receipt
    if expected:
        # Reject consistently mistyped registry targets in actual publications;
        # a repeated same-world binding is still insufficient class evidence.
        from copy import deepcopy
        sidecar = tmp_path/'vfx_completion_observation.json'
        original = sidecar.read_bytes()
        for bad_table in (0x3356f68, 0x2000):
            bad = deepcopy(value)['collection18_witness']
            rows = bad['zero_serial_diagnostic']['rows'] if zero else bad['material_provider_census']['listeners']
            for row in rows:
                row['battle_manager']['table'] = witness['image_base']+bad_table
            sidecar.write_text(json.dumps(dict(value, collection18_witness=bad)))
            rejected = dict(retained)
            retain_vfx_completion_observation(rejected, sidecar, 'vfx-observation-fixture', tmp_path/'objects')
            assert 'vfx_completion_observation_collection_error' in rejected, rejected
            assert 'vfx_collection18_material_provider_census' not in rejected
            assert 'vfx_collection18_zero_serial_diagnostic' not in rejected
            assert rejected['vfx_completion_observation_phase_complete'] is False
        sidecar.write_bytes(original)


@pytest.mark.native_contract
@pytest.mark.parametrize('case', ['material-providers', 'material-providers-unoccupied',
                                  'material-providers-multibucket', 'material-providers-multibucket-chain'])
def test_collection18_selected_material_provider_census(tmp_path, vfx_observer_executable, case):
    """Regression retained from production's one-bucket registry_hash_layout RED.

    The native-shaped sparse/hash registry and provider inputs are fixture data.
    Four live rows use 16 buckets, matching native sizing 141CF3D90; rehash links
    must reach the selected world through a collision, never a dense scan.
    Only the real CallbackExecutorDetour -> C18Before may publish a census.
    External world lookup returns a world, never the selected battle manager.
    Neither this test nor the native stub executes Start, setters or ProcessEvent.
    """
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child = subprocess.run([str(vfx_observer_executable), 'collection18-'+case],
                           cwd=tmp_path, capture_output=True, text=True, timeout=10)
    detail = f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode == 0, detail
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout, detail
    assert 'material native forwarding PASS: input_bytes_unchanged=1 graph_revoked=1' in child.stdout, detail
    assert 'return read attempts=0 guarded-read control=1' in child.stdout, detail
    setup = re.search(r'material fixture graph=(\d+) registry=(\d+) particle_roots_empty=1', child.stdout)
    assert setup, detail
    graph, registry = map(int, setup.groups())
    multibucket = case.startswith('material-providers-multibucket')
    world_address, registry_row_index, registry_bucket = graph+0x3000, 0, 0
    registry_row_address = graph+0x10000
    if multibucket:
        assert ('material registry buckets=16 rows=4 occupancy=15 free_count=0 '
                'target_row=1 head=3 target_bytes_unchanged=1') in child.stdout, detail
        seeded_rows = [tuple(map(int, row)) for row in re.findall(
            r'^material registry row=(\d+) world=(\d+) manager=(\d+) bucket=(\d+) next=(-?\d+)$',
            child.stdout, re.MULTILINE)]
        assert len(seeded_rows) == 4, detail

        def native_bucket(world):
            # Independent arithmetic transcription of142018870 /141D01080.
            # Python integers need explicit uint32 wrapping before each shift.
            mask = 0xffffffff
            i = (world >> 4) & mask
            a = ((0x9e3779b9-i) ^ (i << 8)) & mask
            b = (-(a+i) ^ (a >> 13)) & mask
            c = ((i-a-b) ^ (b >> 12)) & mask
            a = ((a-c-b) ^ (c << 16)) & mask
            b = ((b-a-c) ^ (a >> 5)) & mask
            c = ((c-a-b) ^ (b >> 3)) & mask
            a = ((a-c-b) ^ (c << 10)) & mask
            return ((b-a-c) ^ (a >> 15)) & 15

        heads = [-1]*16
        for expected_index, (index, world, manager, bucket, link) in enumerate(seeded_rows):
            assert index == expected_index, seeded_rows
            assert graph+0x3000 <= world < graph+0x4000 and (world-graph) % 0x40 == 0
            assert bucket == native_bucket(world), seeded_rows
            expected_manager_offset = (0x400, 0, 0x800, 0xc00)[index]
            assert manager == graph+0x4000+expected_manager_offset, seeded_rows
            expected_link = heads[bucket]
            if case.endswith('-chain') and index == 3:
                expected_link = 3  # Only this link differs from native rehash.
            assert link == expected_link, seeded_rows
            heads[bucket] = index
        assert len({row[1] for row in seeded_rows}) == 4, seeded_rows
        world_address = seeded_rows[1][1]
        registry_row_index, registry_row_address = 1, graph+0x10918
        registry_bucket = native_bucket(world_address)
        assert heads[registry_bucket] == 3 and seeded_rows[3][3] == registry_bucket, seeded_rows
    path = tmp_path/'vfx_completion_observation.json'
    value = json.loads(path.read_bytes())
    retained = {}
    retain_vfx_completion_observation(retained, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained, retained
    witness = value['collection18_witness']
    assert witness['header_valid'] is True and witness['returned'] is True
    assert witness['count'] == 2 and witness['writers'] == []
    assert witness['snapshot_complete'] is False
    for flag in ('writer_coverage_proven', 'allocation_generation_proven',
                 'receiver_undo_proven', 'native_completion_proven'):
        assert witness[flag] is False, (flag, witness)
    assert retained['vfx_completion_observation_phase_complete'] is False
    # The retained pre-fix RED reached registry_hash_layout after forwarding and
    # parser retention. Reject that unsupported-layout result for a valid table.
    census = witness.get('material_provider_census')
    assert isinstance(census, dict), 'missing production collection18 ordered material-provider receipt'
    assert census['scope'] == 'entry_visible_providers'
    assert census['covers_native_start_births'] is False
    assert 0 < census['owned_bytes'] <= 256*1024
    assert retained.get('vfx_collection18_material_provider_census') == census, retained
    expected_failure = {'material-providers-unoccupied': 'registry_unoccupied',
                        'material-providers-multibucket-chain': 'registry_chain'}.get(case, '')
    assert census['failure'] == expected_failure, (
        f'collection18 material-provider boundary: expected {expected_failure or "valid entry copy"}, '
        f'observed {census["failure"]}', census)
    assert value['collection18_material_observation_version'] == census['version'] == 3
    assert census['registry_bucket_limit'] == 128
    if expected_failure:
        # Each negative changes one registry field: an occupancy bit or a
        # collision link. Exact world/manager bytes remain present, so a dense
        # scan could still find them; neither case may publish a valid census.
        assert census['entry_copy_valid'] is False
        assert census['registry_bucket_count'] is None
        assert census['descriptor_fields'] is None
        assert census['listeners'] == census['providers'] == census['slots'] == []
        return
    assert census['entry_copy_valid'] is True and census['failure'] == ''
    assert census['registry_bucket_count'] == (16 if multibucket else 1), census
    assert census['descriptor_fields'] == {
        'source_player_index': 1, 'trace_parts_id': 1, 'packed_life': 30,
        'life_stop': False, 'length': 8, 'trace_kind_id': 1, 'clut_id': 0x12,
        'brightness': 1.0,
    }

    def identity(actual, page, index, offset=0):
        assert (actual['address'], actual['index'], actual['serial'], actual['valid']) == (
            graph+page*0x1000+offset, index, 100+index, True), actual

    listeners = census['listeners']
    assert [r['row_index'] for r in listeners] == [1, 0]
    for row, listener in zip((1, 0), listeners):
        assert listener['handle'] == 0x1100+row
        identity(listener['receiver'], row, 7+row)
        identity(listener['world'], 3, 10, world_address-graph-0x3000)
        identity(listener['battle_manager'], 4, 11)
        identity(listener['selected_fighter'], 5, 12)
        identity(listener['trace_manager'], 6, 13)
        identity(listener['trace_root'], 7, 14)
        assert listener['registry'] == registry and listener['registry_row'] == registry_row_address
        assert listener['registry_row_index'] == registry_row_index
        assert listener['registry_bucket'] == registry_bucket, listener
    slots = census['slots']
    assert [(s['listener_row_index'], s['trace_array_offset'], s['trace_ref_index'],
             s['slot_index'], s['source'], s['material']['index']) for s in slots] == [
        (row, array, 0, slot, source, material)
        for row in (1, 0) for array in (0x418, 0x428)
        for slot, source, material in ((0, 'override', 20), (1, 'asset', 21))
    ]
    for s in slots:
        second = int(s['trace_array_offset'] == 0x428)
        identity(s['trace_actor'], 8+second*2, 15+second*2)
        identity(s['provider'], 9+second*2, 16+second*2)
        identity(s['asset'], 12, 19)
        identity(s['material'], 13+s['slot_index'], 20+s['slot_index'])
        assert s['controller'] == graph+0x10100+second*0x200
        assert s['state'] == s['controller']+0x10
        assert s['provider']['table'] == witness['image_base']+0x38829c0
    # Eight occurrences, two distinct material identities: aliases are retained,
    # not collapsed into one receiver/provider/material set or recovery claim.
    assert len(slots) == 8 and len({s['material']['address'] for s in slots}) == 2
    # Feed malformed copies of this actual production publication through the
    # existing reader, reusing a previously valid report to expose stale claims.
    from copy import deepcopy
    original = path.read_bytes()
    for corruption in ('missing', 'version', 'order', 'omitted', 'serial', 'scope', 'births', 'budget',
                       'census-version', 'bucket-limit', 'bucket-count-missing', 'bucket-count-zero',
                       'bucket-count-power', 'bucket-count-capacity', 'bucket-count-type',
                       'bucket-index', 'bucket-index-type'):
        bad = deepcopy(value)
        c = bad['collection18_witness']['material_provider_census']
        if corruption == 'missing':
            bad['collection18_witness'].pop('material_provider_census')
        elif corruption == 'version':
            bad.pop('collection18_material_observation_version')
        elif corruption == 'order':
            c['slots'][0], c['slots'][1] = c['slots'][1], c['slots'][0]
        elif corruption == 'omitted':
            c['slots'].pop()
        elif corruption == 'serial':
            c['slots'][-1]['material']['serial'] += 1
        elif corruption == 'scope':
            c['scope'] = 'complete_affected_materials'
        elif corruption == 'births':
            c['covers_native_start_births'] = True
        elif corruption == 'budget':
            c['owned_bytes'] = 256*1024+1
        elif corruption == 'census-version':
            c['version'] = 1
        elif corruption == 'bucket-limit':
            c['registry_bucket_limit'] = 256
        elif corruption == 'bucket-count-missing':
            c.pop('registry_bucket_count')
        elif corruption == 'bucket-count-zero':
            c['registry_bucket_count'] = 0
        elif corruption == 'bucket-count-power':
            c['registry_bucket_count'] = 3
        elif corruption == 'bucket-count-capacity':
            c['registry_bucket_count'] = 256
        elif corruption == 'bucket-count-type':
            c['registry_bucket_count'] = True
        elif corruption == 'bucket-index':
            # Still in range for16 buckets, but no longer hashes from the world.
            c['listeners'][0]['registry_bucket'] ^= 1
        else:
            c['listeners'][0]['registry_bucket'] = False
        path.write_text(json.dumps(bad))
        rejected = dict(retained)
        retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
        assert 'vfx_completion_observation_collection_error' in rejected, (corruption, rejected)
        assert 'vfx_collection18_material_provider_census' not in rejected, (corruption, rejected)
        assert rejected['vfx_completion_observation_phase_complete'] is False
    if not multibucket:
        # Historical version1 receipts keep their one-bucket scope and never
        # gain a bucket-count observation or recovery claim from this reader.
        legacy = deepcopy(value)
        legacy['collection18_material_observation_version'] = 1
        c = legacy['collection18_witness']['material_provider_census']
        c['version'] = c['registry_bucket_limit'] = 1
        c.pop('registry_bucket_count')
        # Version1 used the former Chara receiver-class assumption.
        for row in c['listeners']:
            row['receiver']['table'] = witness['image_base']+0x3268078
        path.write_text(json.dumps(legacy))
        historical = {}
        retain_vfx_completion_observation(historical, path, 'vfx-observation-fixture', tmp_path/'objects')
        assert 'vfx_completion_observation_collection_error' not in historical, historical
        assert historical['vfx_collection18_material_provider_census'] == c
        assert historical['vfx_completion_observation_phase_complete'] is False
    path.write_bytes(original)
    staged = path.with_name(path.name+'.tmp')
    staged.write_text('{')
    rejected = dict(retained)
    retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_collection18_material_provider_census' not in rejected
    staged.unlink()
    path.unlink()
    retain_vfx_completion_observation(rejected, path, 'vfx-observation-fixture', tmp_path/'objects')
    assert 'vfx_collection18_material_provider_census' not in rejected
    path.write_bytes(original)


@pytest.mark.native_contract
def test_collection18_recursive_append_requires_writer_evidence(tmp_path,vfx_observer_executable):
    """Required RED: entry/return cannot observe an uninstrumented native writer.

    This does not claim a safe complete witness, nor supply fixture-side state
    capture. The actual existing owner forwards all calls; a controlled native
    callee performs the recursive append and invalidates its borrowed pages.
    """
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),'collection18-recursive-append'],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'callback owner forwarding PASS: unrelated=1 input=1 collection18=1' in child.stdout,detail
    assert 'native append PASS: count=2->3 depth=1 calls=1 borrowed_pages_revoked=1' in child.stdout,detail
    path=tmp_path/'vfx_completion_observation.json'
    report=json.loads(path.read_bytes())
    retained={}
    retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
    # A clean old observer phase is not evidence that collection18 had no
    # recursive writer. Missing observation must remain RED, never xfail/pass.
    witness=report.get('collection18_witness')
    assert isinstance(witness,dict), (
        'collection18 recursive append was forwarded but no production witness '
        'records it; old observer phase='+repr(report.get('phase')))
    assert witness['returned'] is True
    assert witness['snapshot_complete'] is False
    assert witness['recursive_writer_observed'] is True
    assert witness['writer_coverage_proven'] is False
    assert witness['receiver_undo_proven'] is False
    assert witness['invalidated'] is True
    assert {0x43d210,0x3a1a70,0x399df0}<={w['entry_rva'] for w in witness['writers']}
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_collection18_writer_receipts_complete'] is True


@pytest.mark.native_contract
@pytest.mark.parametrize('site',range(12))
def test_collection18_writer_signature_admission(tmp_path,vfx_observer_executable,site):
    child=subprocess.run([str(vfx_observer_executable),f'writer-signature-{site}'],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    assert 'signature mismatch forwarded without hooks' in child.stdout
    assert 'native forwarding PASS' in child.stdout
    assert not (tmp_path/'vfx_completion_observation.json').exists()


@pytest.mark.native_contract
@pytest.mark.parametrize('case',('startup-active','recycled-parent','overflow-cross'))
def test_collection18_writer_boundary_gaps(tmp_path,vfx_observer_executable,case):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    child=subprocess.run([str(vfx_observer_executable),'collection18-'+case],
                         cwd=tmp_path,capture_output=True,text=True,timeout=15)
    print(child.stdout)
    assert child.returncode==0,child.stdout+child.stderr
    path=tmp_path/'vfx_completion_observation.json';value=json.loads(path.read_bytes())
    w=value['collection18_witness'];rows=w['writers']
    report={};retain_vfx_completion_observation(report,path,'vfx-observation-fixture',tmp_path/'objects')
    if case=='startup-active':
        assert len(rows)==1 and rows[0]['preexisting'],w
        assert rows[0]['entry_sequence']<w['entry_sequence']<w['return_sequence']<rows[0]['return_sequence']
    elif case=='recycled-parent':
        assert len(rows)==2 and [r['parent_pair'] for r in rows]==[0,1],w
        assert rows[0]['entry_sequence']<rows[1]['entry_sequence']<w['entry_sequence']
    else:
        assert w['overflow'] and w['recursive_writer_observed']
        assert len(rows)==32 and all(r['thread']!=w['thread'] for r in rows)
    assert 'vfx_completion_observation_collection_error' not in report,report
    assert report['vfx_collection18_writer_receipts_complete'] is (case!='overflow-cross')


@pytest.mark.native_contract
def test_collection18_append_covers_native_temporary_storage(tmp_path,vfx_observer_executable):
    child=subprocess.run([str(vfx_observer_executable),'collection18-recursive-append'],
                         cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    w=json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())['collection18_witness']
    assert [r['entry_rva'] for r in w['writers']]==[0x43d210,0x3a1a70,0x399df0],w
    assert w['writers'][1]['parent_pair']==1 and w['writers'][1]['association_unknown']
    assert w['unknown_writer'] is True


@pytest.mark.native_contract
@pytest.mark.parametrize('corruption',('phase','staging','missing','truncated','wrong-run',
    'suffix','overflow-flag','hub','object','late-return','preexisting-ended','close-flag','missing-parent'))
def test_collection18_reader_boundary_gaps(tmp_path,vfx_observer_executable,corruption):
    from copy import deepcopy
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    mode={'overflow-flag':'overflow','preexisting-ended':'preexisting','close-flag':'close-inside'}.get(corruption,'same-address')
    child=subprocess.run([str(vfx_observer_executable),'collection18-'+mode],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    path=tmp_path/'vfx_completion_observation.json';value=json.loads(path.read_bytes())
    good={};retain_vfx_completion_observation(good,path,'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in good,good
    bad=deepcopy(value);w=bad['collection18_witness']
    if corruption=='phase':bad['phase']['close_thread']=0
    elif corruption=='suffix':w['writers'].pop()
    elif corruption=='overflow-flag':w['overflow']=False
    elif corruption=='hub':w['hub']={}
    elif corruption=='object':w['writers'][0]['object']+=0x70
    elif corruption=='late-return':w['writers'][0]['return_sequence']=w['return_sequence']+100
    elif corruption=='preexisting-ended':
        w['writers'][0]['return_sequence']=w['entry_sequence']
        w['entry_sequence']+=100;w['return_sequence']+=100
    elif corruption=='close-flag':w['pending_at_close']=False
    elif corruption=='missing-parent':w['writers'][-1]['parent_pair']=0
    for reuse in (False,True):
        # A successful prior call cannot lend validity to the next publication.
        report=deepcopy(good) if reuse else {}
        if corruption=='missing':path.unlink(missing_ok=True)
        elif corruption=='truncated':path.write_bytes(b'{"version":')
        elif corruption=='wrong-run':
            wrong=deepcopy(bad);wrong['run_id']='different';path.write_text(json.dumps(wrong))
        else:path.write_text(json.dumps(bad))
        if corruption=='staging':path.with_name(path.name+'.tmp').write_bytes(b'partial')
        retain_vfx_completion_observation(report,path,'vfx-observation-fixture',tmp_path/'objects')
        assert report.get('vfx_collection18_writer_receipts_complete') is False,(corruption,reuse,report)
        assert report['vfx_completion_observation_phase_complete'] is False


@pytest.mark.native_contract
@pytest.mark.parametrize('case,sites',[
    ('same-address',[0x3a1910,0x43d210,0x399df0]),
    ('same-handle-copy',[0x17dfea0,0x399420]),
    ('remove',[0x3ca7a0]),('reentry',[]),
    ('overflow',[0x43d210,0x399df0]),('active-overflow',[0x43d210]),
    ('unknown',[0x43d210,0x399df0]),('unknown-layout',[0x43d210,0x399df0]),
    ('close-inside',[0x43d210,0x399df0]),
    ('backing',[0x2050570]),('destroy',[0x3ae520]),
    ('hub-destroy',[0x912df0,0x3ae520]),('grow',[0x2050940,0x2050570]),
    ('capacity',[0x3a1c50,0x2050570]),('row-clear',[0x3a22d0]),
    ('stable',[]),('unrelated-writer',[]),
    ('row-resize',[0x399420]),('row-backing',[0x3a1a70]),
    ('preexisting',[0x399df0]),('cross-thread',[0x399df0]),
    ('writer-return-revoked',[0x399420]),('binder-out-revoked',[0x43d210,0x399df0]),
    ('over-capacity',[]),('unreadable-header',[]),('hub-generation-mismatch',[]),
])
def test_collection18_writer_receipts(tmp_path,vfx_observer_executable,case,sites):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),'collection18-'+case],cwd=tmp_path,
                         capture_output=True,text=True,timeout=10)
    detail=f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'callback owner forwarding PASS' in child.stdout and 'writer native PASS' in child.stdout,detail
    value=json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    witness=value.get('collection18_witness')
    assert isinstance(witness,dict),'missing production collection18 writer receipt'
    assert witness['scope']=='known_native_writers_only'
    assert witness['returned'] is True
    assert witness['snapshot_complete'] is False
    assert witness['entry_state_witness_implemented'] is False
    assert all(witness[k] is False for k in ('writer_coverage_proven','allocation_generation_proven',
                                           'receiver_undo_proven','native_completion_proven'))
    writers=witness['writers']
    assert set(sites)<=set(w['entry_rva'] for w in writers)
    assert len(writers)<=32
    assert all(w['returned'] and w['return_sequence']>w['entry_sequence'] for w in writers)
    assert len({w['entry_sequence'] for w in writers})==len(writers)
    assert all(w['entry_rva'] not in (0x1d38300,0x3fca60) for w in writers)
    assert witness['invalidated'] is (case not in ('stable','unrelated-writer'))
    assert witness['reentry_observed'] is (case=='reentry')
    assert witness['overflow'] is (case in ('overflow','active-overflow'))
    assert witness['unknown_writer'] is (case in ('same-address','overflow','active-overflow','unknown',
                                                'unknown-layout','close-inside','binder-out-revoked'))
    assert witness['header_valid'] is (case not in ('unknown-layout','over-capacity','unreadable-header','hub-generation-mismatch'))
    assert witness['pending_at_close'] is (case=='close-inside')
    assert value['phase']['complete'] is False  # writer-only evidence cannot qualify the full witness
    if case in ('stable','unrelated-writer'):assert writers==[]
    if case=='same-handle-copy':assert writers[1]['parent_pair']==1
    if case in ('preexisting','cross-thread'):
        assert witness['overlap'] and writers[0]['preexisting']
        assert writers[0]['entry_sequence']<witness['entry_sequence']<witness['return_sequence']<writers[0]['return_sequence']
        assert (writers[0]['thread']!=witness['thread']) is (case=='cross-thread')
    report={}
    retain_vfx_completion_observation(report,tmp_path/'vfx_completion_observation.json',
                                     'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in report,report
    assert report['vfx_completion_observation_prefix_valid'] is False
    assert report['vfx_completion_observation_phase_complete'] is False
    assert report['vfx_collection18_writer_receipts_complete'] is (case not in ('overflow','active-overflow','close-inside'))
    if case=='same-address':
        from copy import deepcopy
        # Consumer must reject forged proof, identity, ordering and bounds even
        # though these JSON bytes originated at the production hook.
        for corruption in ('coverage','generation','undo','completion','sequence','parent','bound','reservation','invalidation'):
            bad=deepcopy(value);w=bad['collection18_witness']
            if corruption=='coverage':w['writer_coverage_proven']=True
            elif corruption=='generation':w['allocation_generation_proven']=True
            elif corruption=='undo':w['receiver_undo_proven']=True
            elif corruption=='completion':w['snapshot_complete']=True
            elif corruption=='sequence':w['writers'][1]['entry_sequence']=w['writers'][0]['entry_sequence']
            elif corruption=='parent':w['writers'][0]['parent_pair']=1
            elif corruption=='bound':w['writers']*=16
            elif corruption=='reservation':bad['owned_bytes']-=1
            elif corruption=='invalidation':w['invalidated']=False
            path=tmp_path/(corruption+'.json');path.write_text(json.dumps(bad));rejected={}
            retain_vfx_completion_observation(rejected,path,'vfx-observation-fixture',tmp_path/'objects')
            assert rejected.get('vfx_completion_observation_collection_error'),corruption
            assert rejected['vfx_completion_observation_phase_complete'] is False


@pytest.mark.native_contract
def test_collection18_first_occurrence_and_guarded_read_attempts(tmp_path,vfx_observer_executable):
    for mode in ('first-occurrence','writer-return-revoked','binder-out-revoked'):
        folder=tmp_path/mode;folder.mkdir()
        child=subprocess.run([str(vfx_observer_executable),'collection18-'+mode],
                             cwd=folder,capture_output=True,text=True,timeout=10)
        assert child.returncode==0,child.stdout+child.stderr
        assert 'return read attempts=0 guarded-read control=1' in child.stdout
        if mode=='first-occurrence':
            assert 'first occurrence bytes preserved' in child.stdout
            w=json.loads((folder/'vfx_completion_observation.json').read_bytes())['collection18_witness']
            assert not w['header_valid'] and w['invalidated'] and w['writers']==[]


@pytest.mark.native_contract
def test_vfx_shared_capacity_full_phase_and_forwarding(tmp_path,vfx_observer_executable):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),'writers-capacity-full'],cwd=tmp_path,
                         capture_output=True,text=True,timeout=30)
    detail=f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'native forwarding PASS' in child.stdout,detail
    assert child.stdout.count('native-snapshot')==86,detail
    # Both native helpers also forward after the actual completion block seals
    # the phase. Pre-change production forwards everything but retains only128
    # records; this assertion REDs before schema/accounting differences matter.
    assert 'prunes=85 copies=86 records=512 dropped=0' in child.stdout,detail
    path=tmp_path/'vfx_completion_observation.json'
    raw=path.read_bytes()
    assert 256*1024<len(raw)<=VFX_SIDECAR_BYTES
    assert not path.with_name(path.name+'.tmp').exists()
    report=json.loads(raw)
    assert report['max_events']==512 and report['max_rows']==4
    assert report['owned_bytes']==4*1024*1024
    assert all(report[k]==0 for k in ('dropped','pending_pairs','unstable_records','truncated_records'))
    assert report['persistence_failed'] is False
    assert report['phase']['state']=='closed' and report['phase']['complete'] is True
    assert report['phase']['end_tick']==483 and report['phase']['pending_at_close']==0
    assert report['writer_coverage_proven'] is False and report['synchronized_snapshot'] is False
    events=report['events']
    assert len(events)==512
    assert sum(e['route']=='native_registration' for e in events)==2
    assert sum(e['route']=='dispatch' for e in events)==172
    assert sum(e['route']=='shared_prune' for e in events)==168
    assert sum(e['route']=='shared_copy' for e in events)==170
    assert any(len(e['rows'])==4 and len(e['row_identities'])==4 for e in events)
    assert all(e['rows_complete'] and not e['unstable'] and not e['truncated'] for e in events)
    retained={}
    retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_completion_observation_phase_complete'] is True,retained
    assert retained['vfx_completion_observation_requires_live_status'] is False
    assert retained['vfx_completion_observation']==report


@pytest.mark.native_contract
def test_vfx_completion_observes_true_entries_without_changing_native_calls(tmp_path,vfx_observer_executable):
    child=subprocess.run([str(vfx_observer_executable)],cwd=tmp_path,capture_output=True,text=True,timeout=10)
    detail=f'child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert 'native forwarding PASS' in child.stdout,detail
    assert child.stdout.count('native-snapshot')==2,detail
    # Immutable original RED: native calls forwarded, hooks=0, started=0,
    # child=126. The startup extract still includes the exact production block.
    assert child.returncode==0,detail

    raw=(tmp_path/'vfx_completion_observation.json').read_bytes()
    assert len(raw)<=VFX_SIDECAR_BYTES
    report=json.loads(raw)
    assert report['version']==1 and report['run_id']=='vfx-observation-fixture'
    assert report['pid']>0 and report['observation_only'] is True
    assert report['writer_coverage_proven'] is False and report['synchronized_snapshot'] is False
    assert report['dropped']==0 and report['persistence_failed'] is False
    events=report['events']
    assert len(events)==6  # registration enclosing nested dispatch, then worker dispatch
    actual=dict((k,int(v)) for k,v in re.findall(r'(collection|storage|receiver|payload|main_thread)=(\d+)',child.stdout))
    assert [(e['entry_rva'],e['boundary']) for e in events]==[
        (0x8c9120,'entry'),(0x3d74d0,'entry'),(0x3d74d0,'return'),
        (0x8c9120,'return'),(0x3d74d0,'entry'),(0x3d74d0,'return')]
    for before,after in ((events[0],events[3]),(events[1],events[2]),(events[4],events[5])):
        assert before['pair']==after['pair']
        assert before['thread']==after['thread'] and before['thread']>0
    assert len({e['pair'] for e in events})==3
    assert all(e['thread']==actual['main_thread'] for e in events[:4])
    assert events[4]['thread']!=actual['main_thread']
    assert [e['depth'] for e in events]==[1,2,2,1,1,1]
    assert [e['parent_pair'] for e in events]==[0,events[0]['pair'],events[0]['pair'],0,0,0]
    for i,event in enumerate(events):
        assert event['collection']==actual['collection'] and event['storage']==actual['storage']
        assert event['count']==(0 if i==0 else 1) and event['capacity']==2
        assert event['rows_complete'] is True
        assert event['unstable'] is False and event['truncated'] is False
        assert event['manager']['index']==1 and event['manager']['serial']==31 and event['manager']['valid'] is True
        assert event['rows']==([] if i==0 else [[7,12 if i>=4 else 11,0x100000015]])
        if i:
            assert event['row_identities'][0]['index']==7
            assert event['row_identities'][0]['serial']==11
            assert event['row_identities'][0]['valid'] is (i<4)
        if event['entry_rva']==0x8c9120:
            assert event['receiver']==actual['receiver']
            assert event['descriptor']==0x123456789abcdef0 and event['function_name']==0x100000015
        else:
            assert event['payload']==actual['payload']


@pytest.mark.native_contract
@pytest.mark.parametrize('mode,records,dispatches',(
    ('capacity-window',122,60),
    ('capacity-limit',512,255),
))
def test_vfx_completion_measured_window_capacity_and_durable_sidecar(
        tmp_path,vfx_observer_executable,mode,records,dispatches):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    from deterministic_qualification.replay_evidence import store_bytes

    child=subprocess.run([str(vfx_observer_executable),mode],cwd=tmp_path,
                         capture_output=True,text=True,timeout=30)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'native forwarding PASS' in child.stdout,detail
    assert child.stdout.count('native-snapshot')==dispatches,detail
    status=dict((k,int(v)) for k,v in re.findall(
        r'(records|dropped|persistence_failed|pending_pairs|unstable|truncated)=(\d+)',child.stdout))
    # First expected RED: actual production retains 32 records and drops 45
    # calls for capacity-window, even though every native call still forwards.
    assert status['records']==records,detail
    assert status==dict(records=records,dropped=0,persistence_failed=0,
                        pending_pairs=0,unstable=0,truncated=0),detail

    # Child exits via ExitProcess: no normal shutdown can repair the artifact.
    path=tmp_path/'vfx_completion_observation.json'
    raw=path.read_bytes()
    assert len(raw)<=VFX_SIDECAR_BYTES
    assert not path.with_name(path.name+'.tmp').exists()
    report=json.loads(raw)
    assert report['version']==1 and report['run_id']=='vfx-observation-fixture'
    assert report['max_events']==VFX_EVENT_CAP
    assert report['observation_only'] is True
    assert report['writer_coverage_proven'] is False and report['synchronized_snapshot'] is False
    assert report['dropped']==0 and report['persistence_failed'] is False
    assert report['pending_pairs']==report['unstable_records']==report['truncated_records']==0
    events=report['events']
    assert len(events)==records
    pairs={}
    for event in events:
        pairs.setdefault(event['pair'],[]).append(event)
        assert event['rows_complete'] is True
        assert event['unstable'] is False and event['truncated'] is False
        assert event['manager']['index']==1 and event['manager']['serial']==31
        assert event['manager']['valid'] is True
    assert set(pairs)==set(range(1,records//2+1))
    for before,after in pairs.values():
        assert before['boundary']=='entry' and after['boundary']=='return'
        assert before['thread']==after['thread'] and before['thread']>0
        assert before['entry_rva']==after['entry_rva']
        assert before['parent_pair']==after['parent_pair'] and before['depth']==after['depth']
    assert sum(e['entry_rva']==0x8c9120 for e in events)==2
    assert sum(e['entry_rva']==0x3d74d0 for e in events)==2*dispatches
    for event in events[4:]:
        assert event['rows']==[[7,12,0x100000015]]  # same-count native change retained

    # Exercise the existing production reader with actual child output; no
    # synthetic sidecar or fixture serializer can grant complete retention.
    retained={}
    retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
    assert retained['vfx_completion_observation_raw']==store_bytes(tmp_path/'objects',raw)
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_completion_observation']==report
    assert retained['vfx_completion_observation_prefix_valid'] is True
    assert retained['vfx_completion_observation_requires_live_status'] is True


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('registration-signature','dispatch-signature','prune-signature','copy-signature','disable-signature','completion-signature','task-signature','hub-signature','hub-add-signature','hub-remove-signature','zero','crash','overflow',
                                 'expired-return','truncated','writefail'))
def test_vfx_observation_bounds_failure_and_crash_prefix(tmp_path,vfx_observer_executable,mode):
    child=subprocess.run([str(vfx_observer_executable),mode],cwd=tmp_path,capture_output=True,text=True,timeout=30)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    if mode.endswith('signature'):
        assert 'signature mismatch forwarded without hooks' in child.stdout
        assert 'native forwarding PASS' in child.stdout
        assert not (tmp_path/'vfx_completion_observation.json').exists()
        return
    raw=(tmp_path/'vfx_completion_observation.json').read_bytes()
    assert len(raw)<=VFX_SIDECAR_BYTES
    report=json.loads(raw)
    if mode=='zero':
        assert report['events']==[] and report['pending_pairs']==0
        assert report['persistence_failed'] is False and report['dropped']==0
        return
    if mode=='crash':
        # Abrupt exit inside nested native dispatch, without observer return or
        # shutdown. Both flushed entry witnesses must survive with open pairs.
        assert child.stdout.count('native-snapshot')==1
        assert report['pending_pairs']==2
        assert [e['boundary'] for e in report['events']]==['entry','entry']
        return
    assert 'native forwarding PASS' in child.stdout
    status=dict((k,int(v)) for k,v in re.findall(r'(records|dropped|persistence_failed|pending_pairs|unstable|truncated)=(\d+)',child.stdout))
    assert status['pending_pairs']==0
    if mode=='overflow':
        assert status['records']==VFX_EVENT_CAP and status['dropped']==3
        assert status['persistence_failed']==0
        assert report['max_events']==VFX_EVENT_CAP
        assert len(report['events'])==VFX_EVENT_CAP and report['dropped']==1
        assert report['pending_pairs']==0 and report['persistence_failed'] is False
        assert report['dropped_is_lower_bound'] is True
        assert child.stdout.count('native-snapshot')==258
        from deterministic_qualification.replay_control import retain_vfx_completion_observation
        retained={}
        retain_vfx_completion_observation(retained,tmp_path/'vfx_completion_observation.json',
                                          'vfx-observation-fixture',tmp_path/'objects')
        assert retained['vfx_completion_observation']==report
        assert retained['vfx_completion_observation_prefix_valid'] is False
    elif mode=='expired-return':
        last=report['events'][-1]
        assert last['boundary']=='return' and last['unstable'] is True
        assert last['manager']['valid'] is False and last['header_read'] is False
        assert last['rows']==[] and last['rows_complete'] is False
    elif mode=='truncated':
        assert report['truncated_records']==2
        for event in report['events'][-2:]:
            assert event['count']==6 and event['capacity']==6
            assert len(event['rows'])==4 and event['truncated'] is True and event['rows_complete'] is False
    else:
        assert status['persistence_failed']==1 and child.stdout.count('native-snapshot')==3
        assert len(report['events'])==6  # prior valid publication was preserved
        pending=tmp_path/'vfx_completion_observation.json.tmp'
        assert pending.exists() and len(pending.read_bytes())<=VFX_SIDECAR_BYTES


def test_vfx_collection_retains_crash_prefix_and_journal_restores(tmp_path,monkeypatch):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation
    from deterministic_qualification.run_resources import RunResources
    from deterministic_qualification.replay_evidence import store_bytes
    monkeypatch.setattr('deterministic_qualification.process_control.list_game_processes',lambda:())
    target=tmp_path/'vfx_completion_observation.json'
    staging=tmp_path/'vfx_completion_observation.json.tmp'
    target.write_bytes(b'prior retained evidence')
    journal=tmp_path/'resources.json';resources=RunResources(journal);resources.begin('owned')
    resources.park_report(target);resources.prepare_temporary(staging)
    good=json.dumps(dict(run_id='owned',version=1,pid=123,observation_only=True,
        writer_coverage_proven=False,synchronized_snapshot=False,events=[],dropped=0,
        pending_pairs=0,unstable_records=0,truncated_records=0,persistence_failed=False)).encode()
    for raw in (good,b'{broken',b'x'*(VFX_SIDECAR_BYTES+1),json.dumps(dict(run_id='foreign',version=1)).encode()):
        target.write_bytes(raw);report={'failure':'original'}
        retain_vfx_completion_observation(report,target,'owned',tmp_path/'objects')
        assert report['vfx_completion_observation_raw']==store_bytes(tmp_path/'objects',raw)
        assert report['failure']=='original'
        assert ('vfx_completion_observation' in report)==(raw==good)
    with pytest.raises(RuntimeError,match='outside this run'):RunResources(journal).recover()
    target.write_bytes(good);staging.write_bytes(b'{partial publication')
    report={};retain_vfx_completion_observation(report,target,'owned',tmp_path/'objects')
    assert report['vfx_completion_observation_temporary_raw']==store_bytes(tmp_path/'objects',staging.read_bytes())
    assert report['vfx_completion_observation_prefix_valid'] is False
    assert report['vfx_completion_observation_requires_live_status'] is True
    RunResources(journal).recover()
    assert target.read_bytes()==b'prior retained evidence' and not staging.exists()


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('phase-close','phase-close-pending','phase-close-writefail','phase-close-overlap'))
def test_vfx_completion_phase_closure_at_production_trajectory_boundary(tmp_path,vfx_observer_executable,mode):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),mode],cwd=tmp_path,
                         capture_output=True,text=True,timeout=10)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'native forwarding PASS' in child.stdout,detail
    report=json.loads((tmp_path/'vfx_completion_observation.json').read_bytes())
    if mode=='phase-close':
        # Production RED: 122 valid in-run records become 128 with drops after
        # eight forwarded teardown dispatches. No fixture calls a close API.
        assert len(report['events'])==122,detail
        assert report['dropped']==0,detail
        assert child.stdout.count('native-snapshot')==68,detail
    phase=report['phase']
    assert phase['scope']=='startup_to_trajectory_completion'
    assert phase['boundary']=='CompleteTrajectory.vfx_status'
    assert report['writer_coverage_proven'] is False and report['synchronized_snapshot'] is False
    assert phase['whole_process_coverage'] is False and phase['native_quiescence_proven'] is False
    if mode=='phase-close-writefail':
        # Failed durable closure preserves the earlier open prefix and the
        # staging file; no subsequent native call may make it look closed.
        assert phase['state']=='open'
        assert (tmp_path/'vfx_completion_observation.json.tmp').exists()
        assert 'persistence_failed=1' in child.stdout
    else:
        assert phase['state']=='closed'
        assert phase['end_tick']==337 and phase['close_thread']>0
        assert phase['close_ms']>=report['capture_start_ms']
        assert phase['pending_at_close']==(2 if mode=='phase-close-pending' else 0)
        assert phase['complete'] is (mode!='phase-close-pending')
        assert report['pending_pairs']==0  # returns drain; a bad cutoff stays bad
        assert report['dropped']==0
        if mode=='phase-close-overlap':
            overlapping=[e for e in report['events'] if e['active_other_thread_same_collection']]
            assert len(overlapping)==2  # observed main entry/return overlaps worker call
            assert all(e['thread']==phase['close_thread'] for e in overlapping)
            assert overlapping[0]['boundary']=='entry' and overlapping[1]['boundary']=='return'
    retained={}
    retain_vfx_completion_observation(retained,tmp_path/'vfx_completion_observation.json',
                                      'vfx-observation-fixture',tmp_path/'objects')
    healthy=mode in ('phase-close','phase-close-overlap')
    assert retained['vfx_completion_observation_phase_complete'] is healthy,retained
    assert retained['vfx_completion_observation_requires_live_status'] is (not healthy),retained


@pytest.mark.native_contract
def test_vfx_closed_phase_reader_rejects_inconsistent_receipts(tmp_path,vfx_observer_executable):
    from copy import deepcopy
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),'phase-close'],cwd=tmp_path,
                         capture_output=True,text=True,timeout=10)
    assert child.returncode==0,child.stdout+child.stderr
    path=tmp_path/'vfx_completion_observation.json'
    good=json.loads(path.read_bytes())
    # Actual production output supplies the positive case; mutate only to
    # exercise rejection of missing pairs and contradictory closure claims.
    for corruption in ('pair','complete','whole-process','pending','time','overlap'):
        value=deepcopy(good)
        if corruption=='pair':value['events'].pop()
        elif corruption=='complete':value['phase']['complete']=False
        elif corruption=='whole-process':value['phase']['whole_process_coverage']=True
        elif corruption=='pending':value['phase']['pending_at_close']=1
        elif corruption=='time':value['events'][0]['time_ms']=value['phase']['close_ms']+1
        else:value['events'][0]['active_other_thread_same_collection']=-1
        path.write_text(json.dumps(value))
        retained={}
        retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
        assert retained['vfx_completion_observation_phase_complete'] is False,corruption
        assert retained['vfx_completion_observation_prefix_valid'] is False,corruption
        assert retained['vfx_completion_observation_requires_live_status'] is True,corruption
        assert 'vfx_completion_observation_collection_error' in retained,corruption


@pytest.mark.native_contract
@pytest.mark.parametrize('mode',('writers','writers-filter','writers-overlap','writers-pending',
                                 'writers-expired','writers-overflow'))
def test_vfx_shared_writers_forward_and_observe_production_boundary(tmp_path,vfx_observer_executable,mode):
    from deterministic_qualification.replay_control import retain_vfx_completion_observation

    child=subprocess.run([str(vfx_observer_executable),mode],cwd=tmp_path,
                         capture_output=True,text=True,timeout=30)
    detail=f'{mode} child exit={child.returncode}\n'+child.stdout+child.stderr
    print(detail)
    assert child.returncode==0,detail
    assert 'writer forwarding PASS' in child.stdout,detail
    path=tmp_path/'vfx_completion_observation.json'
    raw=path.read_bytes();assert len(raw)<=VFX_SIDECAR_BYTES
    report=json.loads(raw)
    events=report['events']
    helpers=[e for e in events if e['entry_rva'] in (0x3ea210,0x9481e0)]
    # RED against retained production: native helpers both execute, but no
    # helper entries/returns exist. No compile error or fixture guard grants it.
    assert len(helpers)==(0 if mode=='writers-filter' else 506 if mode=='writers-overflow' else 4),detail
    assert report['shared_writer_observation_version']==1
    assert report['clear_observed'] is False and report['prune_observes_prior_direct_write'] is False
    assert report['writer_coverage_proven'] is False and report['synchronized_snapshot'] is False
    assert report['owned_bytes']==4*1024*1024
    assert report['pending_pairs']==0 and report['persistence_failed'] is False
    phase=report['phase']
    assert phase['state']=='closed' and phase['end_tick']==337
    healthy=mode in ('writers','writers-filter','writers-overlap')
    assert phase['complete'] is healthy
    assert phase['pending_at_close']==(2 if mode=='writers-pending' else 0)
    if mode=='writers-overflow':
        assert len(events)==512 and report['dropped']==7
        assert 'prunes=131 copies=131 records=512 dropped=7' in child.stdout
    else:
        assert report['dropped']==0
    if mode=='writers-filter':
        assert len(events)==6 and 'prunes=5 copies=5' in child.stdout
    elif mode!='writers-overflow':
        assert 'prunes=2 copies=2' in child.stdout
    for event in helpers:
        assert event['caller']>0 and event['caller_route']=='other'
        assert event['thread']==phase['close_thread']
        assert event['manager']['index']==1 and event['manager']['serial']==31
        assert event['route']==('shared_prune' if event['entry_rva']==0x3ea210 else 'shared_copy')
        if event['entry_rva']==0x9481e0:
            assert event['source_collection']>0
        else:
            assert event['source_collection']==0
        if mode in ('writers','writers-pending','writers-expired'):
            assert event['active_dispatch_same_thread_same_collection']==1
            assert event['active_dispatch_other_thread_same_collection']==0
        if mode=='writers-overlap':
            assert event['active_dispatch_same_thread_same_collection']==0
            assert event['active_dispatch_other_thread_same_collection']==1
    if mode in ('writers','writers-overlap','writers-pending','writers-expired'):
        prune_before,prune_after,copy_before,copy_after=helpers
        assert [e['count'] for e in helpers[:3]]==[1,0,0]
        assert prune_before['pair']==prune_after['pair'] and copy_before['pair']==copy_after['pair']
        assert prune_before['caller']==prune_after['caller'] and copy_before['caller']==copy_after['caller']
        if mode=='writers-expired':
            assert copy_after['unstable'] is True and copy_after['header_read'] is False
            assert copy_after['rows']==[] and copy_after['manager']['valid'] is False
            assert report['unstable_records']==2  # copy return and enclosing dispatch return
        else:
            assert copy_after['count']==1 and report['unstable_records']==0
    retained={}
    retain_vfx_completion_observation(retained,path,'vfx-observation-fixture',tmp_path/'objects')
    assert 'vfx_completion_observation_collection_error' not in retained,retained
    assert retained['vfx_completion_observation_phase_complete'] is healthy
    assert retained['vfx_completion_observation_requires_live_status'] is (not healthy)
    if mode=='writers':
        from copy import deepcopy
        for corruption in ('caller-pair','copy-source-pair','dispatch-overlap','route','version','clear'):
            bad=deepcopy(report)
            row=next(e for e in bad['events'] if e['entry_rva']==0x9481e0 and e['boundary']=='return')
            if corruption=='caller-pair':row['caller']+=1
            elif corruption=='copy-source-pair':row['source_collection']+=1
            elif corruption=='dispatch-overlap':row['active_dispatch_same_thread_same_collection']=99
            elif corruption=='route':row['route']='kismet_clear'
            elif corruption=='version':bad.pop('shared_writer_observation_version')
            else:bad['clear_observed']=True
            path.write_text(json.dumps(bad));rejected={}
            retain_vfx_completion_observation(rejected,path,'vfx-observation-fixture',tmp_path/'objects')
            assert rejected['vfx_completion_observation_phase_complete'] is False,corruption
            assert 'vfx_completion_observation_collection_error' in rejected,corruption
        path.write_bytes(raw)
