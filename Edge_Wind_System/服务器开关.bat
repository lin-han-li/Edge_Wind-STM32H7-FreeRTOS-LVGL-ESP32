@echo off
chcp 65001 >nul
setlocal

REM 中文入口：转发到 scripts\edgewind_ctl.bat（脚本实现统一在 scripts/）
cd /d %~dp0
call "%~dp0scripts\\edgewind_ctl.bat"

endlocal

