from tools.deterministic_qualification import process_memory


def test_private_memory_tracker_uses_post_warmup_baseline(monkeypatch):
    now = [0.0]
    values = {10: 100, 20: 200}
    monkeypatch.setattr(process_memory.time, "monotonic", lambda: now[0])
    monkeypatch.setattr(process_memory, "private_bytes", lambda pid: values[pid])
    tracker = process_memory.PrivateMemoryTracker(
        {"host": 10, "sandbox": 20}, 10, started_at=0)
    tracker.sample()
    assert tracker.report()["baseline_private_bytes"] == {}
    tracker.restart_warmup()
    now[0] = 11
    values.update({10: 120, 20: 230})
    tracker.sample()
    now[0] = 12.1
    values.update({10: 130, 20: 250})
    tracker.sample()
    report = tracker.report()
    assert report["baseline_private_bytes"] == {"host": 120, "sandbox": 230}
    assert report["ending_growth_bytes"] == {"host": 10, "sandbox": 20}
