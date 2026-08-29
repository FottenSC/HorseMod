from pathlib import Path

import pytest

from tools.deterministic_qualification import impairment


def test_scoped_filter_names_only_discovered_sc6_udp_ports(monkeypatch, tmp_path):
    tool = tmp_path / "clumsy.exe"
    tool.write_bytes(b"reviewed-tool")
    monkeypatch.setattr(impairment, "_is_administrator", lambda: False)
    instance = impairment.ClumsyImpairment(tool, "loss", 123)
    with pytest.raises(RuntimeError, match="administrator runner"):
        instance.start((100, 200))
    assert impairment._port_filter((27012, 49152)) == (
        "udp and (udp.SrcPort == 27012 or udp.DstPort == 27012 or "
        "udp.SrcPort == 49152 or udp.DstPort == 49152)"
    )


def test_every_required_profile_has_explicit_bounded_arguments():
    assert set(impairment.PROFILE_ARGUMENTS) == {
        "latency", "jitter", "loss", "burst_loss", "reorder",
        "duplicate", "corruption", "disconnect_pre", "disconnect_post",
    }
    assert "--lag-jitter" in impairment.PROFILE_ARGUMENTS["jitter"]
    assert impairment.PROFILE_ARGUMENTS["corruption"][-1] == "ON"
    assert impairment.PROFILE_ARGUMENTS["disconnect_pre"][-1] == "100.0"


def test_clean_profile_never_starts_or_targets_a_process():
    instance = impairment.ClumsyImpairment(Path(), "clean", 1)
    assert instance.start((1, 2)) == {"profile": "clean", "active": False}
    cleanup = instance.stop()
    assert cleanup["rules_removed"] is True
