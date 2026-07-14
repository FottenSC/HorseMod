@echo off
setlocal
cd /d "%~dp0\.."
python tools\rollback_release_gate_run.py --full-player-match-test %*
exit /b %ERRORLEVEL%
