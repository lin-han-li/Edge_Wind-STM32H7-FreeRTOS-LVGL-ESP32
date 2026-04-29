@echo off
setlocal

cd /d %~dp0..
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sim_ctl.ps1" %*

endlocal


