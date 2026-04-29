@echo off
setlocal

REM 兼容入口（根目录）：转发到 scripts\edgewind_ctl.bat
cd /d %~dp0
call "%~dp0scripts\\edgewind_ctl.bat" %*

endlocal


