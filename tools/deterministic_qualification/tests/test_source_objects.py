import json
from pathlib import Path
import pytest
from tools.deterministic_qualification import source_retention as sources
from tools.deterministic_qualification.tests.test_source_retention import repository


@pytest.mark.workflow
def test_content_snapshot_reuses_payload_and_exports_exact_zip(tmp_path):
    root=repository(tmp_path)
    first=sources.retain_sources(root,tmp_path/'evidence')
    assert first['format']=='objects-v2'
    second=sources.retain_sources(root,tmp_path/'evidence')
    assert second['manifest_sha256']==first['manifest_sha256']
    assert second['storage']['new_payload_bytes']==0
    target=tmp_path/'export.zip'
    sources.export_sources(second,target)
    import zipfile
    with zipfile.ZipFile(target) as z:
        assert z.read('workspace/tools/replay_test.py')==(root/'tools/replay_test.py').read_bytes()
    before=target.read_bytes();sources.export_sources(second,target)
    assert target.read_bytes()==before
    (root/'tools/replay_test.py').write_bytes(b'changed')
    third=sources.retain_sources(root,tmp_path/'evidence')
    assert third['storage']['new_payload_bytes']==len(b'changed')


@pytest.mark.workflow
def test_corrupt_payload_blocks_verification_without_replacement(tmp_path):
    root=repository(tmp_path)
    result=sources.retain_sources(root,tmp_path/'evidence')
    manifest=sources.read_manifest(result)
    row=next(r for r in manifest['files'] if not r.get('deleted'))
    import sqlite3
    path=Path(result['objects'])/'objects.sqlite3'
    with sqlite3.connect(path) as db:
        db.execute('UPDATE objects SET payload=? WHERE sha256=?',(b'corrupt',row['sha256']))
    with pytest.raises(RuntimeError): sources.verify_retention(result)
    with pytest.raises(RuntimeError): sources.retain_sources(root,tmp_path/'evidence')
    with sqlite3.connect(path) as db:
        assert db.execute('SELECT payload FROM objects WHERE sha256=?',(row['sha256'],)).fetchone()[0]==b'corrupt'


@pytest.mark.workflow
def test_concurrent_change_rolls_back_without_publishing(tmp_path,monkeypatch):
    root=repository(tmp_path);original=sources.workspace_inputs;calls=0
    def moving(*args):
        nonlocal calls
        calls+=1
        if calls==2:(root/'tools/replay_test.py').write_bytes(b'changed during retention')
        return original(*args)
    monkeypatch.setattr(sources,'workspace_inputs',moving)
    with pytest.raises(RuntimeError,match='changed'):sources.retain_sources(root,tmp_path/'evidence')
    assert not list((tmp_path/'evidence').glob('source-*.json'))
    import sqlite3
    with sqlite3.connect(tmp_path/'evidence/objects/objects.sqlite3') as db:
        assert db.execute('SELECT COUNT(*) FROM objects').fetchone()[0]==0


@pytest.mark.workflow
def test_zip_seed_preserves_original_and_handles_dirty_deletion_external(tmp_path,monkeypatch):
    root=repository(tmp_path);(root/'HorseMod/removed.cpp').unlink()
    (root/'HorseMod/new.hpp').write_bytes(b'dirty untracked')
    external=tmp_path/'external.hpp';external.write_bytes(b'generated dependency')
    monkeypatch.setattr(sources,'build_inputs',lambda *a:({'external/header.hpp':external},{'roots':{'external':str(tmp_path)}}))
    old=sources.retain_sources_zip(root,tmp_path/'legacy',root/'build');before=Path(old['archive']).read_bytes()
    seeded=sources.seed_sources(old,tmp_path/'new');assert seeded['new_payload_bytes']>0
    new=sources.retain_sources(root,tmp_path/'new',root/'build')
    assert new['storage']['new_payload_bytes']==0
    assert Path(old['archive']).read_bytes()==before
    assert any(r.get('deleted') for r in sources.read_manifest(new)['files'])


@pytest.mark.workflow
def test_interrupted_manifest_publication_is_not_a_valid_snapshot(tmp_path,monkeypatch):
    root=repository(tmp_path);original=sources._atomic_payload
    def interrupt(*args):raise KeyboardInterrupt()
    monkeypatch.setattr(sources,'_atomic_payload',interrupt)
    with pytest.raises(KeyboardInterrupt):sources.retain_sources(root,tmp_path/'evidence')
    assert not list((tmp_path/'evidence').glob('source-*.json'))
    monkeypatch.setattr(sources,'_atomic_payload',original)
    resumed=sources.retain_sources(root,tmp_path/'evidence')
    assert resumed['storage']['new_payload_bytes']==0
    sources.verify_retention(resumed)


@pytest.mark.workflow
def test_damaged_database_is_a_verification_error_not_an_unhandled_backend_error(tmp_path):
    result=sources.retain_sources(repository(tmp_path),tmp_path/'evidence')
    (Path(result['objects'])/'objects.sqlite3').write_bytes(b'not a SQLite database')
    with pytest.raises(RuntimeError,match='database'):sources.verify_retention(result)
