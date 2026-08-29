from pathlib import Path

import pytest

from tools.deterministic_qualification.configuration import (
    armed_correction,
    disarm_diagnostics,
    expected_fields,
    is_exact_contract,
    require_disarmed,
)


def test_failure_unconditionally_disarms_diagnostics(tmp_path: Path) -> None:
    config = tmp_path / "rollback.ini"
    config.write_text("version=5\nenabled=false\ntrace=false\n", encoding="utf-8")
    with pytest.raises(RuntimeError):
        with armed_correction(config, 11, 4):
            text = config.read_text(encoding="utf-8")
            assert "forced_depth7_qualification=true" in text
            assert "qualification_depth=11" in text
            raise RuntimeError("interrupted")
    restored = config.read_text(encoding="utf-8")
    assert "enabled=false" in restored
    assert "trace=false" in restored
    assert "correction_probe=false" in restored
    assert "forced_depth7_qualification=false" in restored


def test_disarm_rejects_duplicate_config_keys(tmp_path: Path) -> None:
    config = tmp_path / "rollback.ini"
    config.write_text("trace=true\nTRACE=false\n", encoding="utf-8")
    with pytest.raises(RuntimeError, match="duplicate"):
        disarm_diagnostics(config)


def test_disarm_overrides_enabled_and_verifies_every_diagnostic(tmp_path: Path) -> None:
    config = tmp_path / "rollback.ini"
    config.write_text(
        "enabled=true\ntrace=true\ncorrection_probe=true\n"
        "forced_depth7_qualification=true\nqualification_depth=1\n"
        "qualification_location=4\n",
        encoding="utf-8",
    )
    disarm_diagnostics(config)
    require_disarmed(config)
    restored = config.read_text(encoding="utf-8")
    assert "enabled=false" in restored
    assert "qualification_depth=7" in restored
    assert "qualification_location=2" in restored


def test_exact_config_contract_rejects_unknown_or_reordered_fields() -> None:
    expected = expected_fields(enabled=True, trace=True)
    assert is_exact_contract(dict(expected), expected)
    assert not is_exact_contract({**expected, "unknown": "false"}, expected)
    assert not is_exact_contract(dict(reversed(list(expected.items()))), expected)
