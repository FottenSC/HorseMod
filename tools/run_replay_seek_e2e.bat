@echo off
python "%~dp0replay_seek_test_run.py" --kill-game --build --launch-game --start-replay "%~1" --wait --analyze --strict
