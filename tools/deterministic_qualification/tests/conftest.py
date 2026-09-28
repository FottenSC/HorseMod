"""Local test layers; live/native qualification stays in replay_test's stages."""
from collections import Counter
import json
import os
from pathlib import Path

import pytest


def pytest_addoption(parser):
    parser.addoption('--replay-layer', default='all',
                     choices=('all', 'unit', 'workflow', 'native-contract'))


def pytest_configure(config):
    for name, description in (
        ('unit', 'local Python predicates, parsers and data checks'),
        ('workflow', 'CLI, filesystem, deployment/recovery or orchestration boundaries'),
        ('native_contract', 'compiled production code with explicit fixture service boundaries'),
    ):
        config.addinivalue_line('markers', name + ': ' + description)
    config._replay_layer_counts = {}


def layer_of(item):
    if item.get_closest_marker('native_contract'):
        return 'native-contract'
    if item.get_closest_marker('workflow'):
        return 'workflow'
    return 'unit'


@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(config, items):
    selected, excluded = [], []
    layer = config.getoption('--replay-layer')
    for item in items:
        actual = layer_of(item)
        if actual == 'unit':
            item.add_marker(pytest.mark.unit)
        (selected if layer in ('all', actual) else excluded).append(item)
    if excluded:
        config.hook.pytest_deselected(items=excluded)
    items[:] = selected
    config._replay_layer_counts = dict(Counter(layer_of(item) for item in selected))


@pytest.hookimpl(tryfirst=True)
def pytest_runtest_setup(item):
    # Fail explicitly if a new compiled fixture was accidentally left in a
    # Python layer. This is per pytest child, never persisted in the runner.
    os.environ['HORSE_TEST_LAYER'] = layer_of(item)


def pytest_sessionfinish(session, exitstatus):
    directory = os.environ.get('HORSE_FIXTURE_RECEIPTS')
    if directory:
        path = Path(directory).with_suffix('.layers.json')
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(dict(schema=1, selected=session.config._replay_layer_counts,
                                       requested=session.config.getoption('--replay-layer'),
                                       exit_code=int(exitstatus), live_game=False)), encoding='utf-8')
