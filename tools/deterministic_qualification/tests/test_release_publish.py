import json

import pytest

from tools.deterministic_qualification.release_publish import (
    _certificate, _paired_gate,
)


CASE = {
    "case_id": "case-a", "fighter_order": ["012", "015"],
    "stage_selection_code": "273", "authored_stage_code": "111",
    "stage_package_root": "/Game/DLC/07/Stage/STG011_R",
    "map_path": "/Game/DLC/07/Stage/STG011_R/Maps/STG011_R",
    "native_display_name": "Silver Wolves' Haven",
}


def test_release_certificate_is_canonical_compact_json():
    hashes = {
        "executable": "11" * 32, "dll": "22" * 32,
        "source": "33" * 32, "schema": "44" * 32,
        "candidate": "55" * 32, "regions": "66" * 32,
    }
    encoded = _certificate(CASE, True, hashes, "77" * 32)
    assert b'": ' not in encoded and b", " not in encoded
    value = json.loads(encoded)
    assert value["kind"] == "paired_online_release_case"
    assert value["fighter_order"] == ["012", "015"]
    assert value["qualification_complete"] is True
    assert encoded.endswith(b"}") and not encoded.endswith(b"\n")


def test_paired_release_gate_refuses_a_superficial_pass():
    with pytest.raises(RuntimeError, match="paired impairment profiles missing"):
        _paired_gate([], CASE, "aa" * 32, "bb" * 32)
