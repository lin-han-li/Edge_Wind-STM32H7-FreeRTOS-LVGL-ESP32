@echo off
setlocal

cd /d %~dp0..
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0edgewind_ctl.ps1" %*

endlocal
